#include "test_assert.hpp"

#include "fx/tracer_motion.hpp"

#include <cmath>

namespace slopengine {

namespace {

bool near(float a, float b, float epsilon = 1.0e-4f) {
    return std::fabs(a - b) <= epsilon;
}

void testTracerTravelsAndExpires() {
    TracerStyle style{};
    style.speed = 100.0f;
    style.length = 2.0f;
    Tracer tracer = makeTracer({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 10.0f}, style);
    TracerSpan span{};
    CHECK(!tracerSpan(tracer, span));

    CHECK(advanceTracer(tracer, 0.01f));
    CHECK(tracerSpan(tracer, span));
    CHECK(near(span.tail.z, 0.0f));
    CHECK(near(span.tip.z, 1.0f));
    CHECK(near(span.tailT, 0.5f));
    CHECK(near(span.tipT, 1.0f));

    CHECK(advanceTracer(tracer, 0.04f));
    CHECK(tracerSpan(tracer, span));
    CHECK(near(span.tail.z, 3.0f));
    CHECK(near(span.tip.z, 5.0f));
    CHECK(near(span.tailT, 0.0f));

    CHECK(advanceTracer(tracer, 0.06f));
    CHECK(tracerSpan(tracer, span));
    CHECK(near(span.tip.z, 10.0f));
    CHECK(near(span.tail.z, 9.0f));
    CHECK(near(span.tipT, 0.5f));

    CHECK(!advanceTracer(tracer, 0.02f));
}

void testTracerLeadAndDirection() {
    TracerStyle style{};
    style.speed = 50.0f;
    style.length = 1.0f;
    Tracer tracer = makeTracer({1.0f, 2.0f, 3.0f}, {1.0f, 2.0f, -7.0f}, style, 0.5f);
    TracerSpan span{};
    CHECK(tracerSpan(tracer, span));
    CHECK(near(span.tip.z, 2.5f));
    CHECK(near(span.tail.z, 3.0f));
    CHECK(near(tracer.direction.z, -1.0f));
}

void testTracerMinimumPixelWidth() {
    CHECK(near(tracerWidthAt(0.02f, 0.0f, 100.0f, 90.0f, 1000.0f), 0.02f));
    CHECK(near(tracerWidthAt(0.02f, 2.0f, 1.0f, 90.0f, 1000.0f), 0.02f));
    CHECK(near(tracerWidthAt(0.02f, 2.0f, 100.0f, 90.0f, 1000.0f), 0.4f, 1.0e-3f));
}

} // namespace

void runTracerTests() {
    testTracerTravelsAndExpires();
    testTracerLeadAndDirection();
    testTracerMinimumPixelWidth();
}

}
