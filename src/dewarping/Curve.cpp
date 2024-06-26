// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Curve.h"

#include <QDataStream>

#include "VecNT.h"
#include "XmlMarshaller.h"
#include "XmlUnmarshaller.h"

namespace dewarping {
struct Curve::CloseEnough {
  bool operator()(const QPointF& p1, const QPointF& p2) {
    const QPointF d(p1 - p2);
    return d.x() * d.x() + d.y() * d.y() <= 0.01 * 0.01;
  }
};

Curve::Curve() = default;

Curve::Curve(const std::vector<QPointF>& polyline) : m_polyline(polyline) {}

Curve::Curve(const XSpline& xspline) : m_xspline(xspline), m_polyline(xspline.toPolyline()) {}

Curve::Curve(const QDomElement& el)
    : m_xspline(deserializeXSpline(el.namedItem("xspline").toElement())),
      m_polyline(deserializePolyline(el.namedItem("polyline").toElement())) {}

QDomElement Curve::toXml(QDomDocument& doc, const QString& name) const {
  if (!isValid()) {
    return QDomElement();
  }

  QDomElement el(doc.createElement(name));
  el.appendChild(serializeXSpline(m_xspline, doc, "xspline"));
  el.appendChild(serializePolyline(m_polyline, doc, "polyline"));
  return el;
}

bool Curve::isValid() const {
  return m_polyline.size() > 1 && m_polyline.front() != m_polyline.back();
}

bool Curve::matches(const Curve& other) const {
  return approxPolylineMatch(m_polyline, other.m_polyline);
}

std::vector<QPointF> Curve::deserializePolyline(const QDomElement& el) {
  QByteArray ba(QByteArray::fromBase64(el.text().trimmed().toLatin1()));
#if QT_VERSION_MAJOR == 5
  QDataStream strm(&ba, QIODevice::ReadOnly);
#else
  QDataStream strm(&ba, QIODeviceBase::ReadOnly);
#endif
  strm.setVersion(QDataStream::Qt_4_4);
  strm.setByteOrder(QDataStream::LittleEndian);

  const auto numPoints = static_cast<unsigned int>(ba.size() / 8);
  std::vector<QPointF> points;
  points.reserve(numPoints);

  for (unsigned i = 0; i < numPoints; ++i) {
    float x = 0, y = 0;
    strm >> x >> y;
    points.emplace_back(x, y);
  }
  return points;
}

QDomElement Curve::serializePolyline(const std::vector<QPointF>& polyline, QDomDocument& doc, const QString& name) {
  if (polyline.empty()) {
    return QDomElement();
  }

  QByteArray ba;
  ba.reserve(static_cast<int>(8 * polyline.size()));
#if QT_VERSION_MAJOR == 5
  QDataStream strm(&ba, QIODevice::WriteOnly);
#else
  QDataStream strm(&ba, QIODeviceBase::WriteOnly);
#endif
  strm.setVersion(QDataStream::Qt_4_4);
  strm.setByteOrder(QDataStream::LittleEndian);

  for (const QPointF& pt : polyline) {
    strm << (float) pt.x() << (float) pt.y();
  }

  QDomElement el(doc.createElement(name));
  el.appendChild(doc.createTextNode(QString::fromLatin1(ba.toBase64())));
  return el;
}

bool Curve::approxPolylineMatch(const std::vector<QPointF>& polyline1, const std::vector<QPointF>& polyline2) {
  if (polyline1.size() != polyline2.size()) {
    return false;
  }
  return std::equal(polyline1.begin(), polyline1.end(), polyline2.begin(), CloseEnough());
}

QDomElement Curve::serializeXSpline(const XSpline& xspline, QDomDocument& doc, const QString& name) {
  if (xspline.numControlPoints() == 0) {
    return QDomElement();
  }

  QDomElement el(doc.createElement(name));
  XmlMarshaller marshaller(doc);

  const int numControlPoints = xspline.numControlPoints();
  for (int i = 0; i < numControlPoints; ++i) {
    const QPointF pt(xspline.controlPointPosition(i));
    el.appendChild(marshaller.pointF(pt, "point"));
  }
  return el;
}

XSpline Curve::deserializeXSpline(const QDomElement& el) {
  XSpline xspline;

  const QString pointTagName("point");
  QDomNode node(el.firstChild());
  for (; !node.isNull(); node = node.nextSibling()) {
    if (!node.isElement()) {
      continue;
    }
    if (node.nodeName() != pointTagName) {
      continue;
    }
    xspline.appendControlPoint(XmlUnmarshaller::pointF(node.toElement()), 1);
  }

  if (xspline.numControlPoints() > 0) {
    xspline.setControlPointTension(0, 0);
    xspline.setControlPointTension(xspline.numControlPoints() - 1, 0);
  }
  return xspline;
}

bool Curve::splineHasLoops(const XSpline& spline) {
  const int numControlPoints = spline.numControlPoints();
  const Vec2d mainDirection(spline.pointAt(1) - spline.pointAt(0));

  for (int i = 1; i < numControlPoints; ++i) {
    const QPointF cp1(spline.controlPointPosition(i - 1));
    const QPointF cp2(spline.controlPointPosition(i));
    if (Vec2d(cp2 - cp1).dot(mainDirection) < 0) {
      return true;
    }
#if 0
            const double t1 = spline.controlPointIndexToT(i - 1);
            const double t2 = spline.controlPointIndexToT(i);
            if (Vec2d(spline.pointAt(t2) - spline.pointAt(t1)).dot(mainDirection)) < 0) {
                    return true;
                }
#endif
  }
  return false;
}
}  // namespace dewarping
