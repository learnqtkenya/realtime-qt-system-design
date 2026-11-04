#include "qpwire.hpp"

#include <cmath>

int main()
{
    return qpwire::run(
        [](const std::vector<qpwire::Track> &tracks, qpwire::Overlay &draw, const qpwire::State &) {
            for (const auto &t : tracks) {
                draw.color(t.speed < 5 ? 0xFF34D399 : t.speed < 12 ? 0xFFFBBF24 : 0xFFF87171);
                draw.line(t.x, t.y, t.x + 2 * t.speed * std::cos(t.heading), t.y + 2 * t.speed * std::sin(t.heading));
            }
        },
        "speed vectors");
}
