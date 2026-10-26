#ifndef WOWAI_BOTS_ROUTE_LINE_H
#define WOWAI_BOTS_ROUTE_LINE_H

#include "BotGeometry.h"
#include "BotTypes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// A line of positions with the distance along it, for the route generator
// (a navmesh path) and the route build from a GM trace (RouteTrace.h).
namespace Bots
{
    // The distance from `p` to the segment from `a` to `b`, with the height.
    inline float DistanceToSegment(Position const& p, Position const& a, Position const& b)
    {
        float const vx = b.x - a.x;
        float const vy = b.y - a.y;
        float const vz = b.z - a.z;
        float const length2 = vx * vx + vy * vy + vz * vz;
        float t = 0.0f;
        if (length2 > 0.0f)
            t = std::max(0.0f, std::min(1.0f, ((p.x - a.x) * vx + (p.y - a.y) * vy + (p.z - a.z) * vz) / length2));
        return Distance3d(p, {a.x + vx * t, a.y + vy * t, a.z + vz * t});
    }

    class RouteLine
    {
    public:
        // Each point joins the point before it with a straight segment.
        explicit RouteLine(std::vector<Position> points)
            : RouteLine(points, std::vector<bool>(points.size(), true))
        {
        }

        // `joined[i]` false: no segment from point i - 1 to point i. That
        // jump has no length, so no position of the line lies on it.
        RouteLine(std::vector<Position> points, std::vector<bool> joined) : m_points(std::move(points))
        {
            m_arc.push_back(0.0f);
            for (std::size_t i = 1; i < m_points.size(); ++i)
                m_arc.push_back(m_arc.back() + (joined[i] ? Distance3d(m_points[i - 1], m_points[i]) : 0.0f));
        }

        float Length() const { return m_arc.empty() ? 0.0f : m_arc.back(); }

        // The distance along the line of point i.
        float ArcOf(std::size_t i) const { return m_arc[i]; }

        Position At(float s) const
        {
            if (m_points.size() == 1 || s <= 0.0f)
                return m_points.front();
            std::size_t const i = Segment(s);
            if (i + 1 >= m_points.size())
                return m_points.back();
            float const length = m_arc[i + 1] - m_arc[i];
            float const t = length > 0.0f ? (s - m_arc[i]) / length : 0.0f;
            Position const& a = m_points[i];
            Position const& b = m_points[i + 1];
            return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
        }

        // The flat direction of the line at s, normalized. (1, 0) on a line
        // with no length.
        void Direction(float s, float& dx, float& dy) const
        {
            Position const a = At(std::max(0.0f, s - 1.0f));
            Position const b = At(std::min(Length(), s + 1.0f));
            dx = b.x - a.x;
            dy = b.y - a.y;
            float const norm = std::hypot(dx, dy);
            if (norm < 1e-4f)
            {
                dx = 1.0f;
                dy = 0.0f;
                return;
            }
            dx /= norm;
            dy /= norm;
        }

        // The distances at 1 yard steps, and the end.
        std::vector<float> Samples() const
        {
            std::vector<float> samples;
            for (float s = 0.0f; s < Length(); s += 1.0f)
                samples.push_back(s);
            samples.push_back(Length());
            return samples;
        }

    private:
        std::size_t Segment(float s) const
        {
            auto it = std::upper_bound(m_arc.begin(), m_arc.end(), s);
            return it == m_arc.begin() ? 0 : std::size_t(it - m_arc.begin()) - 1;
        }

        std::vector<Position> m_points;
        std::vector<float> m_arc;
    };
}

#endif
