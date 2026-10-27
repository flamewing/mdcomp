#ifndef MDCOMP_TEST_REFERENCE_MODULES_HH
#define MDCOMP_TEST_REFERENCE_MODULES_HH
#include "fixtures/external_answers.hh"
#include "support/format_observer.hh"
#include "support/wire_builder.hh"

#include <stdexcept>

namespace codec_test {
    inline fixture reference_modules(fixture const& full, size_t padding) {
        if (full.expected.size() != 4096 || !padding) {
            throw std::runtime_error(
                    "Expected a full reference module and positive padding");
        }
        auto const     tail_id = full.id.substr(0, full.id.rfind('-') + 1) + "64";
        fixture const* tail    = nullptr;
        for (auto const& candidate : external_module_blocks()) {
            if (candidate.id == tail_id) {
                tail = &candidate;
            }
        }
        if (!tail || tail->expected.size() != 64) {
            throw std::runtime_error("Missing paired reference tail");
        }
        auto payload = [](fixture const& body) {
            auto const parsed = observe(body.codec, body.compressed);
            if (parsed.decoded != body.expected || !parsed.consumed) {
                throw std::runtime_error("Reference module disagrees with wire model");
            }
            // Strip only transport bytes after the format's semantic end.
            return body.compressed.substr(0, parsed.consumed);
        };
        auto const  block = payload(full), final = payload(*tail);
        std::string body;
        for (unsigned i = 0; i < 2; ++i) {
            body += block;
            body.append((padding - body.size() % padding) % padding, '\0');
        }
        body += final;
        fixture framed;
        framed.id       = full.id + "-two-full-plus-tail";
        framed.codec    = full.codec;
        framed.expected = full.expected + full.expected + tail->expected;
        wire::word(framed.compressed, static_cast<unsigned>(framed.expected.size()));
        framed.compressed += body;
        if (framed.compressed.size() % 2) {
            framed.compressed += '\0';
        }
        framed.settings.module_padding = padding;
        return framed;
    }
}    // namespace codec_test
#endif
