$input v_texcoord0
#include <bgfx_shader.sh>

// Eye adaptation (UE-style auto exposure). One fullscreen pass over a 1x1
// R32F target: grid-samples the lit scene HDR and writes the adapted exposure
// in stops. Ping-pong keeps the previous value in s_autoPrev.
SAMPLER2D(s_autoScene, 0);
SAMPLER2D(s_autoPrev, 1);

// x = calibration (target average luminance), y = dt (s),
// z = speed up (1/s), w = speed down (1/s)
uniform vec4 u_adaptParams;
// x = min EV, y = max EV, z = source aspect (w/h),
// w = 1 histogram metering (10..90 percentile), 0 = plain log average
uniform vec4 u_adaptRange;

// Histogram window in log2 luminance; samples outside it land in the end bins.
const int kBins = 64;
const float kLogMin = -10.0;
const float kLogMax = 10.0;

void main() {
    float prevEv = texture2D(s_autoPrev, vec2(0.5, 0.5)).x;

    // Deterministic grid over the frame: 32 rows, aspect-corrected column count.
    // Coarse by design; the ping-pong feedback smooths it.
    const int tapsY = 32;
    int tapsX = max(8, int(float(tapsY) * u_adaptRange.z));
    float logSum = 0.0;
    float bins[kBins];
    for (int b = 0; b < kBins; ++b) bins[b] = 0.0;
    int count = 0;
    for (int y = 0; y < tapsY; ++y) {
        float v = (float(y) + 0.5) / float(tapsY);
        for (int x = 0; x < tapsX; ++x) {
            float u = (float(x) + 0.5) / float(tapsX);
            vec3 c = texture2D(s_autoScene, vec2(u, v)).rgb;
            float lum = max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 1e-5);
            float lg = log2(lum);
            logSum += lg;
            int bin = int(clamp((lg - kLogMin) / (kLogMax - kLogMin) * float(kBins),
                                0.0, float(kBins - 1)));
            bins[bin] += 1.0;
            ++count;
        }
    }

    float avgLogLum = logSum / float(max(count, 1));
    if (u_adaptRange.w > 0.5) {
        // Mean of the 10..90 percentile population: deep shadows and the sun
        // no longer dominate the average.
        float lowCount = 0.1 * float(count);
        float highCount = 0.9 * float(count);
        float binWidth = (kLogMax - kLogMin) / float(kBins);
        float running = 0.0;
        float weightSum = 0.0;
        float lumSum = 0.0;
        for (int b = 0; b < kBins; ++b) {
            float lo = running;
            float hi = running + bins[b];
            running = hi;
            float inside = max(0.0, min(hi, highCount) - max(lo, lowCount));
            lumSum += inside * (kLogMin + (float(b) + 0.5) * binWidth);
            weightSum += inside;
        }
        if (weightSum > 0.0) avgLogLum = lumSum / weightSum;
    }

    // Stops required to bring the scene to the calibration luminance.
    float targetEv = log2(max(u_adaptParams.x, 1e-4)) - avgLogLum;
    targetEv = clamp(targetEv, u_adaptRange.x, u_adaptRange.y);

    float speed = targetEv > prevEv ? u_adaptParams.z : u_adaptParams.w;
    float nextEv = prevEv +
                   (targetEv - prevEv) *
                       (1.0 - exp(-speed * max(u_adaptParams.y, 1e-6)));

    gl_FragColor = vec4(nextEv, 0.0, 0.0, 1.0);
}
