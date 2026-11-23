/******************************************************************************
 * @file            kbd.c
 *****************************************************************************/
#include    <corazon/hid/kbd.h>
#include    <corazon/tasking/irq.h>
#include    <corazon/utils/misc.h>
#include    <corazon/acpi.h>
#include    <corazon/console.h>
#include    <corazon/regs.h>

#include    <stdint.h>

static const uint32_t key_to_ascii_default[__KEY_LAST] = {

    0 /* invalid key */,
    
    'a',  'b',  'c',  'd',  'e',  'f',  'g',  'h',  'i',  'j',
    'k',  'l',  'm',  'n',  'o',  'p',  'q',  'r',  's',  't',
    'u',  'v',  'w',  'x',  'y',  'z',  '0',  '1',  '2',  '3',
    '4',  '5',  '6',  '7',  '8',  '9',  '`',  '-',  '=',  '\\',
    127,  ' ',  0,    '\t', '[',  ']',  0,    0,    0,    0,
    0,    0,    0,    0,    0,    '\n', 0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    '/',  '*',  '-',  '+',  '\n', '.',  '0',
    '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  ';',
   '\'',  ',',  '.',  '/'

};

static const uint32_t key_to_ascii_shift[__KEY_LAST] = {

    0 /* invalid key */,
    
    'A',  'B',  'C',  'D',  'E',  'F',  'G',  'H',  'I',  'J',
    'K',  'L',  'M',  'N',  'O',  'P',  'Q',  'R',  'S',  'T',
    'U',  'V',  'W',  'X',  'Y',  'Z',  ')',  '!',  '"',  '#',
    '$',  '%',  '^',  '&',  '*',  '(',  '~',  '_',  '+',  '|',
    127,  ' ',  0,    '\t', '{',  '}',  0,    0,    0,    0,
    0,    0,    0,    0,    0,    '\n', 0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    ':',
    '@',  '<',  '>',  '?',  0

};

static const uint32_t key_to_ascii_alt_gr[__KEY_LAST] = {

    0 /* invalid key */,
    
    0,      0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0x00B5, 0x00F1, 0,      0x00F6, 0x00E4, 0,      0x00DF, 0,
    0,      0,      0,      0,      0x00FC, 0x00E6, 0,      0x00A1, 0x00B2, 0x00B3,
    0,      0x20AC, 0x00BC, 0x00BD, 0,      0,      0,      0x00A5, 'x',    0x00AC,
    0,      0,      0,      0,      0x00AB, 0x00BB, 0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,      0x00B6,
    0,      0x00E7, 0,      0x00BF

};

static const uint32_t key_to_ascii_shift_alt_gr[__KEY_LAST] = {

    0 /* invalid key */,
    
    0,      0,      0x00A2, 0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0x00D1, 0,      0x00D6, 0x00C4, 0,      0x00A7, 0,
    0,      0,      0,      0,      0x00DC, 0x00C6, 0,      0,      0,      0,
    0x00A3, 0,      0,      0,      0,      0,      0,      0,      0x00F7, 0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,      0x00B0,
    0,      0x00C7, 0,      0

};

static const key_t scancode_to_key_default[] = {

    0,          KEY_ESC,    KEY_1,      KEY_2,      KEY_3,      KEY_4,       KEY_5,        KEY_6,
    KEY_7,      KEY_8,      KEY_9,      KEY_0,      KEY_MINUS,  KEY_EQUAL,   KEY_BACK,     KEY_TAB,
    KEY_Q,      KEY_W,      KEY_E,      KEY_R,      KEY_T,      KEY_Y,       KEY_U,        KEY_I,
    KEY_O,      KEY_P,      KEY_OSQBRA, KEY_CSQBRA, KEY_ENTER,  KEY_LCTRL,   KEY_A,        KEY_S,
    KEY_D,      KEY_F,      KEY_G,      KEY_H,      KEY_J,      KEY_K,       KEY_L,        KEY_SEMI,
    KEY_APPOS,  KEY_ACC,    KEY_LSHIFT, KEY_BACKSL, KEY_Z,      KEY_X,       KEY_C,        KEY_V,
    KEY_B,      KEY_N,      KEY_M,      KEY_COMMA,  KEY_DOT,    KEY_SLASH,   KEY_RSHIFT,   KEY_KPMULT,
    KEY_LALT,   KEY_SPACE,  KEY_CAPS,   KEY_F1,     KEY_F2,     KEY_F3,      KEY_F4,       KEY_F5,
    KEY_F6,     KEY_F7,     KEY_F8,     KEY_F9,     KEY_F10,    KEY_NUM,     KEY_SCROLL,   KEY_KP7,
    KEY_KP8,    KEY_KP9,    KEY_KPMIN,  KEY_KP4,    KEY_KP5,    KEY_KP6,     KEY_KPPLUS,   KEY_KP1,
    KEY_KP2,    KEY_KP3,    KEY_KP0,    KEY_KPDOT,  0,          0,           0,            KEY_F11,// 5
    KEY_F12,    0,          0,          0,          0,          0,           0,            0,
    0,          0,          0,          0,          0,          0,           0,            0,
    0,          0,          0,          0,          0,          0,           0,            0,
    0,          0,          0,          0,          0,          0,           0,            0,
    0,          0,          0,          0,          0,          0,           0,            0,
};

static const key_t scancode_to_key_e0[] = {

    0,          0,          0,          0,          0,          0,           0,            0,
    0,          0,          0,          0,          0,          0,           0,            0,
    0,          0,          0,          0,          0,          0,           0,            0,
    0,          0,          0,          0,          KEY_KPEN,   KEY_RCTRL,   0,            0,
    0,          0,          0,          0,          0,          0,           0,            0,
    0,          0,          0,          0,          0,          0,           0,            0,
    0,          0,          0,          0,          0,          KEY_KPSLASH, 0,            KEY_PRINT,
    KEY_ALTGR,  0,          0,          0,          0,          0,           0,            0,
    0,          0,          0,          0,          0,          0,           0,            KEY_HOME,
    KEY_ARRU,   KEY_PGUP,   0,          KEY_ARRL,   0,          KEY_ARRR,    0,            KEY_END,
    KEY_ARRD,   KEY_PGDWN,  KEY_INS,    KEY_DEL,    0,          0,           0,            0,
    0,          0,          0,          KEY_LGUI,   KEY_RGUI,   0,           0,            0,
    0,          0,          0,          0,          0,          0,           0,            0,
    0,          0,          0,          0,          0,          0,           0,            0,
    0,          0,          0,          0,          0,          0,           0,            0,
    0,          0,          0,          0,          0,          0,           0,            0,

};

static int pressed_keys[__KEY_LAST] = { 0 };
static uint8_t led = 0;

static uint8_t led_pending = 0;
static uint8_t led_phase = 0;
static uint8_t led_value = 0;
static uint8_t led_retries = 0;

#if     defined (__AMD64__)
static int keyboard_ioapic_routed = 0;
#endif

static uint8_t inportb (uint16_t port) {

    uint8_t ret;
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (ret)
        : "d" (port)
    );
    
    return ret;

}

static void kbd_update_led (void) {

    led_pending = 1;
    
    if (led_phase || (inportb (0x64) & 2)) {
        return;
    }

    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" ((uint8_t) 0xED), "Nd" ((uint16_t) 0x60)
    );
    
    led_phase = 1;
    
    led_pending = 0;
    led_retries = 0;

}

static int kbd_led_reply (uint8_t response) {

    uint8_t value;
    
    if (!led_phase || (response != 0xFA && response != 0xFE)) {
        return 0;
    }
    
    if (response == 0xFA) {
    
        if (led_phase == 1) {
        
            led_value = led;
            
            led_phase = 2;
            led_retries = 0;
            
            value = led_value;
        
        } else {
        
            led_phase = 0;
            
            if (led_pending || led != led_value) {
                kbd_update_led ();
            }
            
            return 1;
        
        }
    
    } else {
    
        if (led_retries == 3) {
        
            led_phase = 0;
            return 1;
        
        }
        
        led_retries++;
        value = led_phase == 1 ? 0xED : led_value;
    
    }
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (value), "Nd" ((uint16_t) 0x60)
    );
    
    return 1;

}

static uint8_t get_scancode (void) {
    return inportb (0x60);
}

static key_t scancode_to_key (uint8_t scancode, int *make) {

    static uint8_t prev = 0, counter = 1;
    key_t key = __KEY_INVALID;
    
    *make = !(scancode & 0x80);
    
    if (scancode == 0xE0) {
        prev = 0xE0;
    } else if (scancode == 0xE1) {
    
        counter = 1;
        prev = 0xE1;
    
    } else {
    
        if (prev == 0xE0) {
        
            key = scancode_to_key_e0[scancode & 0x7F];
            prev = 0;
        
        } else if (prev == 0xE1) {
        
            counter++;
            
            if (counter == 3) {
            
                prev = 0;
                return KEY_PAUSE;
            
            }
        
        } else {
        
            key = scancode_to_key_default[scancode & 0x7F];
            prev = 0;
        
        }
    
    }
    
    return key;

}

static char key_to_ascii (key_t key) {

    int shift = pressed_keys[KEY_LSHIFT] || pressed_keys[KEY_RSHIFT];
    uint32_t retchar = 0;
    
    if ((led & (1U << 2)) && key >= KEY_A && key <= KEY_Z) {
        shift = !shift;
    }
    
    if (pressed_keys[KEY_ALTGR]) {
    
        if (shift) {
            retchar = key_to_ascii_shift_alt_gr[key];
        }
        
        if (!shift || retchar == 0) {
            retchar = key_to_ascii_alt_gr[key];
        }
    
    }
    
    if (!pressed_keys[KEY_ALTGR] || retchar == 0) {
    
        if (shift) {
            retchar = key_to_ascii_shift[key];
        }
        
        if (!shift || retchar == 0) {
            retchar = key_to_ascii_default[key];
        }
    
    }
    
    if (pressed_keys[KEY_LALT]) {
    
        // console switching maybe???
    
    }
    
    if (pressed_keys[KEY_RCTRL] || pressed_keys[KEY_LCTRL]) {
    
        if (key >= KEY_A && key <= KEY_Z) {
            return (char) (key - KEY_A + 1);
        }
        
        if (key == KEY_ESC || retchar == 'e') {
        
            // kill process.
            return 0;
        
        }
    
    }
    
    return retchar;

}

static void key_pressed (key_t key, int make) {

    char ascii;
    
    if (key == __KEY_INVALID) {
        return;
    }
    
    pressed_keys[key] = make;
    
    if (make) {
    
        if (key == KEY_CAPS) {
        
            led ^= (1U << 2);
            kbd_update_led ();
        
        } else if (key == KEY_NUM) {
        
            led ^= (1U << 1);
            kbd_update_led ();
        
        } else if (key == KEY_SCROLL) {
        
            led ^= (1U << 0);
            kbd_update_led ();
        
        }
    
    } else {
        return;
    }
    
    switch (key) {
    
        case KEY_ARRU:
        
            ascii = 'A';
            break;
        
        case KEY_ARRD:
        
            ascii = 'B';
            break;
        
        case KEY_ARRR:
        
            ascii = 'C';
            break;
        
        case KEY_ARRL:
        
            ascii = 'D';
            break;
        
        case KEY_DEL:
        
            ascii = '3';
            break;
        
        case KEY_INS:
        
            ascii = '2';
            break;
        
        case KEY_PGUP:
        
            ascii = '5';
            break;
        
        case KEY_PGDWN:
        
            ascii = '6';
            break;
        
        case KEY_HOME:
        
            ascii = 'H';
            break;
        
        case KEY_END:
        
            ascii = 'F';
            break;
        
        case KEY_KP0:
        
            ascii = !(led & (1U << 1)) ? '2' : 0;
            break;
        
        case KEY_KP1:
        
            ascii = !(led & (1U << 1)) ? 'F' : 0;
            break;
        
        case KEY_KP2:
        
            ascii = !(led & (1U << 1)) ? 'B' : 0;
            break;
        
        case KEY_KP3:
        
            ascii = !(led & (1U << 1)) ? '6' : 0;
            break;
        
        case KEY_KP4:
        
            ascii = !(led & (1U << 1)) ? 'D' : 0;
            break;
        
        case KEY_KP5:
        
            if (!(led & (1U << 1))) {
                return;
            }
            
            ascii = 0;
            break;
        
        case KEY_KP6:
        
            ascii = !(led & (1U << 1)) ? 'C' : 0;
            break;
        
        case KEY_KP7:
        
            ascii = !(led & (1U << 1)) ? 'H' : 0;
            break;
        
        case KEY_KP8:
        
            ascii = !(led & (1U << 1)) ? 'A' : 0;
            break;
        
        case KEY_KP9:
        
            ascii = !(led & (1U << 1)) ? '5' : 0;
            break;
        
        case KEY_KPDOT:
        
            ascii = !(led & (1U << 1)) ? '3' : 0;
            break;
        
        default:
        
            ascii = 0;
            break;
    
    }
    
    if (ascii) {
    
        console_input_char ('\x1B');
        console_input_char ('[');
        console_input_char (ascii);
        
        if (ascii == '2' || ascii == '3' || ascii == '5' || ascii == '6') {
            console_input_char ('~');
        }
        
        return;
    
    }
    
    if ((ascii = key_to_ascii (key))) {
        console_input_char (ascii);
    }

}

static int kbd_handler (struct regs *r) {

    uint8_t scancode;
    key_t key;
    
    int make;
    size_t i;
    
    (void) r;
    
    for (i = 0; i < 32 && (inportb (0x64) & 1); i++) {
    
        scancode = get_scancode ();
        
        if (kbd_led_reply (scancode)) {
            continue;
        }
        
        make = 0;
        
        key = scancode_to_key (scancode, &make);
        key_pressed (key, make);
    
    }
    
    if (led_pending && !led_phase) {
        kbd_update_led ();
    }
    
#if     defined (__AMD64__)
    if (keyboard_ioapic_routed) {
        acpi_lapic_ack ();
    }
#endif
    
    return 0;

}

void kbd_install (void) {

    size_t i;
    
    for (i = 0; i < 256 && (inportb (0x64) & 1); i++) {
        inportb (0x60);
    }
    
    irq_install_handler (1, kbd_handler, "keyboard");
    
#if     defined (__AMD64__)
    keyboard_ioapic_routed = acpi_route_legacy_irq (1);
    
    if (!keyboard_ioapic_routed) {
    
        uint8_t mask = inportb (0x21);
        
        __asm__ __volatile__ (
            "outb %0, %1"
            :
            : "a" ((uint8_t) (mask & ~(1U << 1))), "Nd" ((uint16_t) 0x21)
        );
    
    }
#endif

}
