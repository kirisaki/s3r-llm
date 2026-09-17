#include "chat.hpp"
#include "display.hpp"
#include "llm.hpp"
#include "serial.hpp"

extern "C" void app_main(void)
{
    serial::init();
    display::init();
    chat::init();

    static char prompt[256];
    for (;;) {
        serial::write("> ");
        if (serial::read_line(prompt, sizeof(prompt)) == 0) {
            continue;
        }
        chat::add(chat::Speaker::User, prompt);

        chat::begin(chat::Speaker::Assistant);
        llm::generate(prompt, [](const char *piece) {
            chat::append(piece);
            serial::write(piece);
        });
        serial::write("\r\n");
    }
}
