// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Binarize.h"

#include <QDebug>
#include <cassert>
#include <cmath>
#include <stdexcept>

#include "BinaryImage.h"
#include "Grayscale.h"
#include "IntegralImage.h"

namespace imageproc {
BinaryImage binarizeOtsu(const QImage& src) {
  return BinaryImage(src, BinaryThreshold::otsuThreshold(src));
}

BinaryImage binarizeMokji(const QImage& src, const unsigned maxEdgeWidth, const unsigned minEdgeMagnitude) {
  const BinaryThreshold threshold(BinaryThreshold::mokjiThreshold(src, maxEdgeWidth, minEdgeMagnitude));
  return BinaryImage(src, threshold);
}

/*
 * sauvola = mean * (1.0 + k * (stderr / 128.0 - 1.0)), k = 0.34
 * modification by zvezdochiot:
 * sauvola = mean * (1.0 + k * ((stderr + delta) / 128.0 - 1.0)), k = 0.34, delta = 0
 */
BinaryImage binarizeSauvola(const QImage& src,
                            const QSize windowSize,
                            const double k,
                            const double delta) {
  if (windowSize.isEmpty()) {
    throw std::invalid_argument("binarizeSauvola: invalid windowSize");
  }

  if (src.isNull()) {
    return BinaryImage();
  }

  const QImage gray(toGrayscale(src));
  if (gray.isNull()) {
    return BinaryImage();
  }
  const int w = gray.width();
  const int h = gray.height();

  IntegralImage<uint32_t> integralImage(w, h);
  IntegralImage<uint64_t> integralSqimage(w, h);

  const uint8_t* grayLine = gray.bits();
  const int grayBpl = gray.bytesPerLine();

  for (int y = 0; y < h; ++y) {
    integralImage.beginRow();
    integralSqimage.beginRow();
    for (int x = 0; x < w; ++x) {
      const uint32_t pixel = grayLine[x];
      integralImage.push(pixel);
      integralSqimage.push(pixel * pixel);
    }
    grayLine += grayBpl;
  }

  const int windowLowerHalf = windowSize.height() >> 1;
  const int windowUpperHalf = windowSize.height() - windowLowerHalf;
  const int windowLeftHalf = windowSize.width() >> 1;
  const int windowRightHalf = windowSize.width() - windowLeftHalf;

  BinaryImage bwImg(w, h);
  uint32_t* bwLine = bwImg.data();
  const int bwWpl = bwImg.wordsPerLine();

  grayLine = gray.bits();
  for (int y = 0; y < h; ++y) {
    const int top = std::max(0, y - windowLowerHalf);
    const int bottom = std::min(h, y + windowUpperHalf);  // exclusive
    for (int x = 0; x < w; ++x) {
      const int left = std::max(0, x - windowLeftHalf);
      const int right = std::min(w, x + windowRightHalf);  // exclusive
      const int area = (bottom - top) * (right - left);
      assert(area > 0);  // because windowSize > 0 and w > 0 and h > 0
      const QRect rect(left, top, right - left, bottom - top);
      const double windowSum = integralImage.sum(rect);
      const double windowSqsum = integralSqimage.sum(rect);

      const double rArea = 1.0 / area;
      const double mean = windowSum * rArea;
      const double sqmean = windowSqsum * rArea;

      const double variance = sqmean - mean * mean;
      const double deviation = std::sqrt(std::fabs(variance));

      const double threshold = mean * (1.0 + k * ((deviation + delta) / 128.0 - 1.0));

      const uint32_t msb = uint32_t(1) << 31;
      const uint32_t mask = msb >> (x & 31);
      if (int(grayLine[x]) < threshold) {
        // black
        bwLine[x >> 5] |= mask;
      } else {
        // white
        bwLine[x >> 5] &= ~mask;
      }
    }
    grayLine += grayBpl;
    bwLine += bwWpl;
  }
  return bwImg;
}  // binarizeSauvola

/*
 * wolf = mean - k * (mean - min_v) * (1.0 - stderr / stdmax), k = 0.3
 * modification by zvezdochiot:
 * wolf = mean - k * (mean - min_v) * (1.0 - (stderr / stdmax + delta / 128.0), k = 0.3, delta = 0
 */
BinaryImage binarizeWolf(const QImage& src,
                         const QSize windowSize,
                         const unsigned char lowerBound,
                         const unsigned char upperBound,
                         const double k,
                         const double delta) {
  if (windowSize.isEmpty()) {
    throw std::invalid_argument("binarizeWolf: invalid windowSize");
  }

  if (src.isNull()) {
    return BinaryImage();
  }

  const QImage gray(toGrayscale(src));
  if (gray.isNull()) {
    return BinaryImage();
  }
  const int w = gray.width();
  const int h = gray.height();

  IntegralImage<uint32_t> integralImage(w, h);
  IntegralImage<uint64_t> integralSqimage(w, h);

  const uint8_t* grayLine = gray.bits();
  const int grayBpl = gray.bytesPerLine();

  uint32_t minGrayLevel = 255;

  for (int y = 0; y < h; ++y) {
    integralImage.beginRow();
    integralSqimage.beginRow();
    for (int x = 0; x < w; ++x) {
      const uint32_t pixel = grayLine[x];
      integralImage.push(pixel);
      integralSqimage.push(pixel * pixel);
      minGrayLevel = std::min(minGrayLevel, pixel);
    }
    grayLine += grayBpl;
  }

  const int windowLowerHalf = windowSize.height() >> 1;
  const int windowUpperHalf = windowSize.height() - windowLowerHalf;
  const int windowLeftHalf = windowSize.width() >> 1;
  const int windowRightHalf = windowSize.width() - windowLeftHalf;

  std::vector<float> means(w * h, 0);
  std::vector<float> deviations(w * h, 0);

  double maxDeviation = 0;

  for (int y = 0; y < h; ++y) {
    const int top = std::max(0, y - windowLowerHalf);
    const int bottom = std::min(h, y + windowUpperHalf);  // exclusive
    for (int x = 0; x < w; ++x) {
      const int left = std::max(0, x - windowLeftHalf);
      const int right = std::min(w, x + windowRightHalf);  // exclusive
      const int area = (bottom - top) * (right - left);
      assert(area > 0);  // because windowSize > 0 and w > 0 and h > 0
      const QRect rect(left, top, right - left, bottom - top);
      const double windowSum = integralImage.sum(rect);
      const double windowSqsum = integralSqimage.sum(rect);

      const double rArea = 1.0 / area;
      const double mean = windowSum * rArea;
      const double sqmean = windowSqsum * rArea;

      const double variance = sqmean - mean * mean;
      const double deviation = std::sqrt(std::fabs(variance));
      maxDeviation = std::max(maxDeviation, deviation);
      means[w * y + x] = (float) mean;
      deviations[w * y + x] = (float) deviation;
    }
  }

  // TODO: integral images can be disposed at this point.

  BinaryImage bwImg(w, h);
  uint32_t* bwLine = bwImg.data();
  const int bwWpl = bwImg.wordsPerLine();

  grayLine = gray.bits();
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float mean = means[y * w + x];
      const float deviation = deviations[y * w + x];
      const double a = 1.0 - (deviation / maxDeviation + (double) delta / 128.0);
      const double threshold = mean - k * a * (mean - minGrayLevel);

      const uint32_t msb = uint32_t(1) << 31;
      const uint32_t mask = msb >> (x & 31);
      if ((grayLine[x] < lowerBound) || ((grayLine[x] <= upperBound) && (int(grayLine[x]) < threshold))) {
        // black
        bwLine[x >> 5] |= mask;
      } else {
        // white
        bwLine[x >> 5] &= ~mask;
      }
    }
    grayLine += grayBpl;
    bwLine += bwWpl;
  }
  return bwImg;
}  // binarizeWolf

BinaryImage binarizeBradley(const QImage& src, const QSize windowSize, const double k, const double delta) {
  if (windowSize.isEmpty()) {
    throw std::invalid_argument("binarizeBradley: invalid windowSize");
  }

  if (src.isNull()) {
    return BinaryImage();
  }

  QImage gray(toGrayscale(src));
  if (gray.isNull()) {
    return BinaryImage();
  }
  const int w = gray.width();
  const int h = gray.height();

  IntegralImage<uint32_t> integralImage(w, h);

  uint8_t* grayLine = gray.bits();
  const int grayBpl = gray.bytesPerLine();

  for (int y = 0; y < h; ++y) {
    integralImage.beginRow();
    for (int x = 0; x < w; ++x) {
      const uint32_t pixel = grayLine[x];
      integralImage.push(pixel);
    }
    grayLine += grayBpl;
  }

  const int windowLowerHalf = windowSize.height() >> 1;
  const int windowUpperHalf = windowSize.height() - windowLowerHalf;
  const int windowLeftHalf = windowSize.width() >> 1;
  const int windowRightHalf = windowSize.width() - windowLeftHalf;

  BinaryImage bwImg(w, h);
  uint32_t* bwLine = bwImg.data();
  const int bwWpl = bwImg.wordsPerLine();

  grayLine = gray.bits();
  for (int y = 0; y < h; ++y) {
    const int top = std::max(0, y - windowLowerHalf);
    const int bottom = std::min(h, y + windowUpperHalf);  // exclusive
    for (int x = 0; x < w; ++x) {
      const int left = std::max(0, x - windowLeftHalf);
      const int right = std::min(w, x + windowRightHalf);  // exclusive
      const int area = (bottom - top) * (right - left);
      assert(area > 0);  // because windowSize > 0 and w > 0 and h > 0
      const QRect rect(left, top, right - left, bottom - top);
      const double windowSum = integralImage.sum(rect);

      const double rArea = 1.0 / area;
      const double mean = windowSum * rArea;
      const double threshold = (k < 1.0) ? (mean * (1.0 - k)) : 0;
      const uint32_t msb = uint32_t(1) << 31;
      const uint32_t mask = msb >> (x & 31);
      if (int(grayLine[x]) < (threshold + delta)) {
        // black
        bwLine[x >> 5] |= mask;
      } else {
        // white
        bwLine[x >> 5] &= ~mask;
      }
    }
    grayLine += grayBpl;
    bwLine += bwWpl;
  }
  return bwImg;
}  // binarizeBradley

/*
 * grad = mean * k + meanG * (1.0 - k), meanG = mean(I * G) / mean(G), G = |I - mean|, k = 0.75
 * modification by zvezdochiot:
 * mean = mean + delta, delta = 0
 */
BinaryImage binarizeGrad(const QImage& src,
                         const QSize windowSize,
                         const unsigned char lowerBound,
                         const unsigned char upperBound,
                         const double k,
                         const double delta) {
  if (windowSize.isEmpty()) {
    throw std::invalid_argument("binarizeGrad: invalid windowSize");
  }

  if (src.isNull()) {
    return BinaryImage();
  }

  QImage gray(toGrayscale(src));
  if (gray.isNull()) {
    return BinaryImage();
  }
  QImage gmean(toGrayscale(src));
  if (gmean.isNull()) {
    return BinaryImage();
  }
  const int w = gray.width();
  const int h = gray.height();

  const uint8_t* grayLine = gray.bits();
  const int grayBpl = gray.bytesPerLine();

  uint8_t* gmeanLine = gmean.bits();
  const int gmeanBpl = gmean.bytesPerLine();

  IntegralImage<uint32_t> integralImage(w, h);

  for (int y = 0; y < h; ++y) {
    integralImage.beginRow();
    for (int x = 0; x < w; ++x) {
      const uint32_t pixel = grayLine[x];
      integralImage.push(pixel);
    }
    grayLine += grayBpl;
  }

  const int windowLowerHalf = windowSize.height() >> 1;
  const int windowUpperHalf = windowSize.height() - windowLowerHalf;
  const int windowLeftHalf = windowSize.width() >> 1;
  const int windowRightHalf = windowSize.width() - windowLeftHalf;

  for (int y = 0; y < h; ++y) {
    const int top = std::max(0, y - windowLowerHalf);
    const int bottom = std::min(h, y + windowUpperHalf);  // exclusive
    for (int x = 0; x < w; ++x) {
      const int left = std::max(0, x - windowLeftHalf);
      const int right = std::min(w, x + windowRightHalf);  // exclusive
      const int area = (bottom - top) * (right - left);
      assert(area > 0);  // because windowSize > 0 and w > 0 and h > 0
      const QRect rect(left, top, right - left, bottom - top);
      const double windowSum = integralImage.sum(rect);

      const double rArea = 1.0 / area;
      const double mean = windowSum * rArea + 0.5 + delta;
      const int imean = (int) ((mean < 0.0) ? 0.0 : (mean < 255.0) ? mean : 255.0);
      gmeanLine[x] = imean;
    }
    gmeanLine += gmeanBpl;
  }

  double gvalue = 127.5;
  double sum_g = 0.0, sum_gi = 0.0;
  grayLine = gray.bits();
  gmeanLine = gmean.bits();
  for (int y = 0; y < h; y++) {
    double sum_gl = 0.0;
    double sum_gil = 0.0;
    for (int x = 0; x < w; x++) {
      double gi = grayLine[x];
      double g = gmeanLine[x];
      g -= gi;
      g = (g < 0.0) ? -g : g;
      gi *= g;
      sum_gl += g;
      sum_gil += gi;
    }
    sum_g += sum_gl;
    sum_gi += sum_gil;
    grayLine += grayBpl;
    gmeanLine += gmeanBpl;
  }
  gvalue = (sum_g > 0.0) ? (sum_gi / sum_g) : gvalue;

  double const meanGrad = gvalue * (1.0 - k);

  BinaryImage bwImg(w, h);
  uint32_t* bwLine = bwImg.data();
  const int bwWpl = bwImg.wordsPerLine();

  grayLine = gray.bits();
  gmeanLine = gmean.bits();
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const double origin = grayLine[x];
      const double mean = gmeanLine[x];
      const double threshold = meanGrad + mean * k;
      const uint32_t msb = uint32_t(1) << 31;
      const uint32_t mask = msb >> (x & 31);
      if ((grayLine[x] < lowerBound) || ((grayLine[x] <= upperBound) && (origin < threshold))) {
        // black
        bwLine[x >> 5] |= mask;
      } else {
        // white
        bwLine[x >> 5] &= ~mask;
      }
    }
    grayLine += grayBpl;
    gmeanLine += gmeanBpl;
    bwLine += bwWpl;
  }
  return bwImg;
}  // binarizeGrad

/*
 * edgediv == EdgeDiv image prefilter before the Otsu threshold
 */
BinaryImage binarizeEdgeDiv(const QImage& src,
                            const QSize windowSize,
                            const double kep,
                            const double kbd,
                            const double delta) {
  if (windowSize.isEmpty()) {
    throw std::invalid_argument("binarizeBlurDiv: invalid windowSize");
  }

  if (src.isNull()) {
    return BinaryImage();
  }

  QImage gray(toGrayscale(src));
  if (gray.isNull()) {
    return BinaryImage();
  }
  const int w = gray.width();
  const int h = gray.height();

  IntegralImage<uint32_t> integralImage(w, h);

  uint8_t* grayLine = gray.bits();
  const int grayBpl = gray.bytesPerLine();

  for (int y = 0; y < h; ++y) {
    integralImage.beginRow();
    for (int x = 0; x < w; ++x) {
      const uint32_t pixel = grayLine[x];
      integralImage.push(pixel);
    }
    grayLine += grayBpl;
  }

  const int windowLowerHalf = windowSize.height() >> 1;
  const int windowUpperHalf = windowSize.height() - windowLowerHalf;
  const int windowLeftHalf = windowSize.width() >> 1;
  const int windowRightHalf = windowSize.width() - windowLeftHalf;

  grayLine = gray.bits();
  for (int y = 0; y < h; ++y) {
    const int top = std::max(0, y - windowLowerHalf);
    const int bottom = std::min(h, y + windowUpperHalf);  // exclusive
    for (int x = 0; x < w; ++x) {
      const int left = std::max(0, x - windowLeftHalf);
      const int right = std::min(w, x + windowRightHalf);  // exclusive
      const int area = (bottom - top) * (right - left);
      assert(area > 0);  // because windowSize > 0 and w > 0 and h > 0
      const QRect rect(left, top, right - left, bottom - top);
      const double windowSum = integralImage.sum(rect);

      const double rArea = 1.0 / area;
      const double mean = windowSum * rArea;
      const double origin = grayLine[x];
      double retval = origin;
      if (kep > 0.0) {
        // EdgePlus
        // edge = I / blur (shift = -0.5) {0.0 .. >1.0}, mean value = 0.5
        const double edge = (retval + 1) / (mean + 1) - 0.5;
        // edgeplus = I * edge, mean value = 0.5 * mean(I)
        const double edgeplus = origin * edge;
        // return k * edgeplus + (1 - k) * I
        retval = kep * edgeplus + (1.0 - kep) * origin;
      }
      if (kbd > 0.0) {
        // BlurDiv
        // edge = blur / I (shift = -0.5) {0.0 .. >1.0}, mean value = 0.5
        const double edgeinv = (mean + 1) / (retval + 1) - 0.5;
        // edgenorm = edge * k + max * (1 - k), mean value = {0.5 .. 1.0} * mean(I)
        const double edgenorm = kbd * edgeinv + (1.0 - kbd);
        // return I / edgenorm
        retval = (edgenorm > 0.0) ? (origin / edgenorm) : origin;
      }
      // trim value {0..255}
      retval = (retval < 0.0) ? 0.0 : (retval < 255.0) ? retval : 255.0;
      grayLine[x] = (int) retval;
    }
    grayLine += grayBpl;
  }
  return BinaryImage(gray, (BinaryThreshold::otsuThreshold(gray) + delta));
}  // binarizeBlurDiv

BinaryImage peakThreshold(const QImage& image) {
  return BinaryImage(image, BinaryThreshold::peakThreshold(image));
}
}  // namespace imageproc
