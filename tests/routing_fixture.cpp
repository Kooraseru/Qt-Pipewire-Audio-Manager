// Test-only signal generators and meters. Never loaded into the desktop graph.
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <QCoreApplication>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <atomic>
#include <array>
#include <cstdio>
#include <memory>
#include <vector>

struct Endpoint {
    pw_filter *filter = nullptr;
    const char *name;
    bool output;
    float amplitude;
    int channels;
    std::array<void *, 2> ports{};
    std::array<std::atomic<float>, 2> mean{};
    std::atomic<unsigned> cycles{0};
    ~Endpoint() { if (filter) pw_filter_destroy(filter); }
    static void process(void *data, spa_io_position *position) {
        auto &e = *static_cast<Endpoint *>(data);
        const auto count = position->clock.duration;
        for (int c = 0; c < e.channels; ++c) {
            auto *samples = static_cast<float *>(pw_filter_get_dsp_buffer(e.ports[c], count));
            if (!samples) continue;
            if (e.output) {
                for (unsigned n = 0; n < count; ++n) samples[n] = e.amplitude;
            } else {
                float sum = 0;
                for (unsigned n = 0; n < count; ++n) sum += samples[n];
                e.mean[c].store(count ? sum / count : 0, std::memory_order_relaxed);
            }
        }
        ++e.cycles;
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    pw_init(nullptr, nullptr);
    auto *loop = pw_thread_loop_new("routing-test", nullptr);
    std::vector<std::unique_ptr<Endpoint>> endpoints;
    const auto add = [&](const char *name, const char *media, bool output, float amplitude, int channels, const char *group = "") {
        auto endpoint = std::make_unique<Endpoint>();
        endpoint->name = name; endpoint->output = output; endpoint->amplitude = amplitude; endpoint->channels = channels;
        static const pw_filter_events events = [] { pw_filter_events e{}; e.version = PW_VERSION_FILTER_EVENTS; e.process = Endpoint::process; return e; }();
        auto *p = pw_properties_new("node.name", name, "node.description", name, "media.class", media,
            "node.autoconnect", "false", "node.want-driver", "true", "node.link-group", group, nullptr);
        if (QString::fromUtf8(media) == "Stream/Output/Audio") pw_properties_set(p, "application.id", "org.plasmaaudiomanager.RoutingTest");
        endpoint->filter = pw_filter_new_simple(pw_thread_loop_get_loop(loop), name, p, &events, endpoint.get());
        for (int c = 0; c < channels; ++c) {
            const auto *channel = channels == 1 ? "MONO" : c == 0 ? "FL" : "FR";
            endpoint->ports[c] = pw_filter_add_port(endpoint->filter, output ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT,
                PW_FILTER_PORT_FLAG_MAP_BUFFERS, 1, pw_properties_new("format.dsp", "32 bit float mono audio",
                "port.name", channel, "audio.channel", channel, nullptr), nullptr, 0);
        }
        if (pw_filter_connect(endpoint->filter, PW_FILTER_FLAG_RT_PROCESS, nullptr, 0) < 0) std::abort();
        endpoints.push_back(std::move(endpoint));
    };
    const auto arguments = app.arguments();
    const auto appIndex = arguments.indexOf("--app-name");
    const auto appName = (appIndex >= 0 ? arguments.value(appIndex + 1) : QString("test-app")).toUtf8();
    add(appName.constData(), "Stream/Output/Audio", true, .125f, 2);
    if (arguments.contains("--duplicate-app")) add("test-app-duplicate", "Stream/Output/Audio", true, .125f, 2);
    add("test-mic", "Audio/Source", true, .25f, 2);
    add("test-mono", "Audio/Source", true, .0625f, 1);
    add("test-output-a", "Audio/Sink", false, 0, 2);
    add("test-output-b", "Audio/Sink", false, 0, 2);
    add("test-output-mono", "Audio/Sink", false, 0, 1);
    add("test-cable-input", "Audio/Source", true, .0f, 2, "test-feedback");
    add("test-cable-output", "Audio/Sink", false, 0, 2, "test-feedback");
    pw_thread_loop_start(loop);
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        QJsonObject stats;
        for (const auto &e : endpoints) if (!e->output) {
            QJsonArray channels;
            for (int c = 0; c < e->channels; ++c) channels.append(e->mean[c].load());
            stats[e->name] = QJsonObject{{"channels", channels}, {"cycles", int(e->cycles.load())}};
        }
        const auto json = QJsonDocument(stats).toJson(QJsonDocument::Compact);
        std::puts(json.constData()); std::fflush(stdout);
    });
    timer.start(100);
    return app.exec();
}
