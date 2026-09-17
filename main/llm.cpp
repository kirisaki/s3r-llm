#include "llm.hpp"

#include <cassert>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "llm.h"

namespace llm
{

namespace
{

constexpr char TAG[] = "llm";

constexpr float TOP_P = 0.9f;
// Positions that have to be left for the reply, or the conversation starts over
constexpr int MIN_REPLY = 48;
// Every token reads all of the weights, and PSRAM is the faster place to read
// them from: about 8.5 tok/s instead of 4.5 tok/s straight from flash. Costs
// the size of the model partition in PSRAM and 0.3s at boot.
constexpr bool COPY_MODEL_TO_PSRAM = true;

Transformer transformer;
Tokenizer tokenizer;
Sampler sampler;

// Position of the next token; everything before it is in the KV cache
int pos = 0;

// The training format is "User: <u>\nBot: <b><eos>\nUser: <u>\nBot: ..."
constexpr int MAX_TOKENS = 512;
int tokens[MAX_TOKENS];

const uint8_t *map_partition(const char *name, size_t *size)
{
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, name);
    assert(part);
    const void *data = nullptr;
    esp_partition_mmap_handle_t handle;
    ESP_ERROR_CHECK(esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &data, &handle));
    *size = part->size;
    return static_cast<const uint8_t *>(data);
}

// Appends the tokens of text, as many as fit below limit
int encode(const char *text, int n, int limit)
{
    // llm_encode() needs room for one token per byte
    static int buf[MAX_TOKENS];
    assert(strlen(text) + 8 <= MAX_TOKENS);
    int len = 0;
    llm_encode(&tokenizer, text, 0, 0, buf, &len);
    for (int i = 0; i < len && n < limit; i++) {
        tokens[n++] = buf[i];
    }
    return n;
}

int encode_prompt(const char *prompt, bool first_turn)
{
    const int seq_len = transformer.config.seq_len;
    int n = 0;
    if (!first_turn) {
        tokens[n++] = tokenizer.eos_id;
        n = encode("\n", n, seq_len);
    }
    n = encode("User: ", n, seq_len);
    // An overlong message is cut off rather than left without room for a reply
    n = encode(prompt, n, seq_len - MIN_REPLY - 4);
    n = encode("\nBot:", n, seq_len);
    return n;
}

void emit_ascii(const char *piece, const Sink &sink)
{
    char buf[32];
    size_t len = 0;
    for (; *piece && len + 1 < sizeof(buf); piece++) {
        const bool printable = *piece >= 0x20 && *piece <= 0x7E;
        if (printable || *piece == '\n') {
            buf[len++] = *piece;
        }
    }
    buf[len] = '\0';
    if (len > 0) {
        sink(buf);
    }
}

} // namespace

void init()
{
    size_t model_size, tokenizer_size;
    const uint8_t *model = map_partition("model", &model_size);
    const uint8_t *tokenizer_data = map_partition("tokenizer", &tokenizer_size);

    if (COPY_MODEL_TO_PSRAM) {
        auto *copy = static_cast<uint8_t *>(heap_caps_aligned_alloc(16, model_size, MALLOC_CAP_SPIRAM));
        assert(copy);
        memcpy(copy, model, model_size);
        model = copy;
    }

    // The window covers every position the model has, so it never has to slide
    const bool ok = llm_init_embedded(&transformer, model, model_size, 0) &&
                    llm_tokenizer_from_memory(&tokenizer, tokenizer_data, tokenizer_size, 0);
    if (!ok) {
        ESP_LOGE(TAG, "no usable model in flash, run `idf.py tokenizer-flash model-flash`");
        abort();
    }
    llm_build_sampler(&sampler, transformer.config.vocab_size, DEFAULT_TEMPERATURE, TOP_P, esp_random());

    const Config &c = transformer.config;
    ESP_LOGI(TAG, "dim=%d layers=%d heads=%d vocab=%d seq_len=%d kv=%d", c.dim, c.n_layers, c.n_heads,
             c.vocab_size, c.seq_len, transformer.kv_seq_len);
}

void set_temperature(float temperature)
{
    sampler.temperature = temperature;
}

Stats generate(const char *prompt, const Sink &sink)
{
    const int seq_len = transformer.config.seq_len;

    int n = encode_prompt(prompt, pos == 0);
    if (pos + n + MIN_REPLY > seq_len) {
        // Out of positions: forget the conversation so far
        pos = 0;
        n = encode_prompt(prompt, true);
    }

    Stats stats = {};
    stats.prompt_tokens = n;
    const int64_t start = esp_timer_get_time();

    float *logits = nullptr;
    for (int i = 0; i < n; i++) {
        logits = llm_forward(&transformer, tokens[i], pos++);
        vTaskDelay(1); // let the idle task feed the watchdog
    }
    const int64_t prompt_done = esp_timer_get_time();

    // A newline is held back until it is clear that the model is not starting
    // a "\nUser:" turn of its own instead of ending the reply with EOS
    bool pending_newline = false;
    while (pos < seq_len - 1) {
        const int token = llm_sample(&sampler, logits);
        if (token == tokenizer.eos_id) {
            break;
        }
        char scratch[32];
        const char *piece = llm_decode(&tokenizer, 0, token, scratch, sizeof(scratch));
        if (pending_newline) {
            if (strncmp(piece, "User", 4) == 0) {
                pos--; // take the newline back out of the context
                break;
            }
            sink("\n");
            pending_newline = false;
        }
        if (strcmp(piece, "\n") == 0) {
            pending_newline = true;
        } else {
            emit_ascii(piece, sink);
        }
        stats.reply_tokens++;

        logits = llm_forward(&transformer, token, pos++);
        vTaskDelay(1);
    }
    if (pos >= seq_len - 1) {
        pos = 0;
    }

    stats.prompt_ms = (prompt_done - start) / 1000;
    stats.reply_ms = (esp_timer_get_time() - prompt_done) / 1000;
    return stats;
}

} // namespace llm
