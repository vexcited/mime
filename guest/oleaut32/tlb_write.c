/*
 * creating type libraries: ICreateTypeLib2 and ICreateTypeInfo2 fill the model, and SaveAllChanges writes it as an
 * msft file with hashed names and guids and its own tables of types, strings and constants, readable by tlb_read.c
 * and by windows. LHashValOfNameSys and CreateDispTypeInfo live here too.
 */
#include "typelib.h"

#define HRESULT_WRITEFAULT ((HRESULT)0x8003001DL)

/* ------------------------------------------------------------------ name hashing */

/*
 * the name hash a type library stores. Starting from a fixed seed, each byte sets the value to the byte's weight plus
 * 37 times the old value, in 32 bits. The hash is that modulo 65599 in the low 16 bits, with the locale class number
 * above and one more bit for the Macintosh. Byte weights depend on the class and on whether the library targets the
 * Macintosh.
 */
#define HASH_SEED 233495534u
#define HASH_MODULUS 65599u
#define HASH_MAC_BIT 0x10000u

/* neutral class weights: letters weigh as their capital, accented letters as the base letter, and a few more. */
static const BYTE g_weight_neutral[256] = {
    /* 0x00 */ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    /* 0x10 */ 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    /* 0x20 */ 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 0,
    /* 0x30 */ 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    /* 0x40 */ 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    /* 0x50 */ 80, 81, 82, 83, 84, 85, 86, 86, 88, 85, 90, 91, 92, 93, 94, 95,
    /* 0x60 */ 96, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    /* 0x70 */ 80, 81, 82, 83, 84, 85, 86, 86, 88, 85, 90, 123, 124, 125, 126, 127,
    /* 0x80 */ 127, 127, 130, 70, 132, 133, 134, 135, 127, 137, 83, 139, 140, 127, 127, 127,
    /* 0x90 */ 127, 145, 146, 147, 148, 149, 150, 150, 152, 153, 83, 155, 140, 127, 127, 85,
    /* 0xa0 */ 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 65, 171, 172, 150, 174, 175,
    /* 0xb0 */ 176, 177, 50, 51, 180, 181, 182, 183, 184, 49, 79, 187, 188, 189, 190, 191,
    /* 0xc0 */ 65, 65, 65, 65, 65, 65, 65, 67, 69, 69, 69, 69, 73, 73, 73, 73,
    /* 0xd0 */ 68, 78, 79, 79, 79, 79, 79, 215, 79, 85, 85, 85, 85, 85, 222, 223,
    /* 0xe0 */ 65, 65, 65, 65, 65, 65, 65, 67, 69, 69, 69, 69, 73, 73, 73, 73,
    /* 0xf0 */ 68, 78, 79, 79, 79, 79, 79, 247, 79, 85, 85, 85, 85, 85, 222, 85,
};

/* the same for the Macintosh, whose upper half is a different character set. */
static const BYTE g_weight_neutral_mac[256] = {
    /* 0x00 */ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    /* 0x10 */ 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    /* 0x20 */ 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 0,
    /* 0x30 */ 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    /* 0x40 */ 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    /* 0x50 */ 80, 81, 82, 83, 84, 85, 86, 86, 88, 85, 90, 91, 92, 93, 94, 95,
    /* 0x60 */ 96, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    /* 0x70 */ 80, 81, 82, 83, 84, 85, 86, 86, 88, 85, 90, 123, 124, 125, 126, 127,
    /* 0x80 */ 65, 65, 67, 69, 78, 79, 85, 65, 65, 65, 65, 65, 65, 67, 69, 69,
    /* 0x90 */ 69, 69, 73, 73, 73, 73, 78, 79, 79, 79, 79, 79, 85, 85, 85, 85,
    /* 0xa0 */ 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 65, 79,
    /* 0xb0 */ 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 65, 79, 189, 65, 79,
    /* 0xc0 */ 192, 193, 194, 195, 70, 197, 198, 199, 200, 201, 202, 65, 65, 79, 206, 206,
    /* 0xd0 */ 208, 208, 210, 211, 212, 213, 214, 215, 85, 85, 218, 219, 220, 221, 63, 63,
    /* 0xe0 */ 224, 225, 226, 227, 228, 65, 69, 65, 69, 69, 73, 73, 73, 73, 79, 79,
    /* 0xf0 */ 63, 79, 85, 85, 85, 73, 127, 247, 127, 249, 250, 251, 63, 253, 254, 127,
};

/* a byte with a class specific weight. */
typedef struct {
    BYTE Byte, Weight;
} WEIGHT_CHANGE;

/* a double byte character with one or two weights of its own, the second 0 for none. */
typedef struct {
    BYTE Lead, Trail, First, Second;
} DBCS_WEIGHT;

static const WEIGHT_CHANGE g_changes_central_european[] = {
    {87, 87}, {89, 89}, {119, 87}, {121, 89}, {131, 127}, {140, 83},
    {141, 84}, {142, 90}, {143, 90}, {152, 127}, {156, 83}, {157, 84},
    {158, 90}, {159, 90}, {161, 127}, {163, 76}, {165, 65}, {170, 83},
    {175, 90}, {178, 178}, {179, 76}, {185, 65}, {186, 83}, {188, 76},
    {190, 76}, {191, 90}, {192, 82}, {197, 76}, {198, 67}, {200, 67},
    {204, 69}, {207, 68}, {208, 208}, {210, 78}, {216, 82}, {221, 89},
    {222, 84}, {224, 82}, {229, 76}, {230, 67}, {232, 67}, {236, 69},
    {239, 68}, {240, 208}, {242, 78}, {248, 82}, {253, 89}, {254, 84},
    {255, 255},
};

static const WEIGHT_CHANGE g_changes_russian[] = {
    {87, 87}, {89, 89}, {119, 87}, {121, 89}, {159, 89}, {198, 198},
    {208, 208}, {221, 89}, {230, 198}, {240, 208}, {253, 89}, {255, 89},
};

static const WEIGHT_CHANGE g_changes_greek[] = {
    {47, 47}, {87, 87}, {89, 89}, {119, 87}, {121, 89}, {127, 0},
    {128, 0}, {129, 0}, {136, 0}, {138, 0}, {140, 0}, {141, 0},
    {142, 0}, {143, 0}, {144, 0}, {150, 45}, {151, 45}, {152, 0},
    {154, 0}, {156, 0}, {157, 0}, {158, 0}, {159, 0}, {160, 9},
    {170, 0}, {173, 45}, {175, 45}, {185, 185}, {186, 186}, {192, 186},
    {193, 162}, {194, 194}, {195, 195}, {196, 196}, {197, 184}, {198, 198},
    {199, 185}, {200, 200}, {201, 186}, {202, 202}, {203, 203}, {204, 204},
    {205, 205}, {206, 206}, {207, 188}, {208, 208}, {209, 209}, {210, 0},
    {211, 211}, {212, 212}, {213, 190}, {214, 214}, {216, 216}, {217, 191},
    {218, 186}, {219, 190}, {220, 162}, {221, 184}, {222, 185}, {223, 186},
    {224, 190}, {225, 162}, {226, 194}, {227, 195}, {228, 196}, {229, 184},
    {230, 198}, {231, 185}, {232, 200}, {233, 186}, {234, 202}, {235, 203},
    {236, 204}, {237, 205}, {238, 206}, {239, 188}, {240, 208}, {241, 209},
    {242, 211}, {243, 211}, {244, 212}, {245, 190}, {246, 214}, {247, 215},
    {248, 216}, {249, 191}, {250, 186}, {251, 190}, {252, 188}, {253, 190},
    {254, 191}, {255, 0},
};

static const WEIGHT_CHANGE g_changes_icelandic[] = {
    {47, 47}, {87, 87}, {89, 89}, {119, 87}, {121, 89}, {127, 0},
    {128, 0}, {129, 0}, {136, 0}, {141, 0}, {142, 0}, {143, 0},
    {144, 0}, {150, 45}, {151, 45}, {157, 0}, {158, 0}, {159, 89},
    {160, 9}, {173, 45}, {193, 193}, {198, 198}, {201, 201}, {205, 205},
    {208, 208}, {211, 211}, {214, 214}, {216, 214}, {218, 218}, {221, 221},
    {225, 193}, {230, 198}, {233, 201}, {237, 205}, {240, 208}, {243, 211},
    {246, 214}, {248, 214}, {250, 218}, {253, 221}, {255, 89},
};

static const WEIGHT_CHANGE g_changes_turkish[] = {
    {47, 47}, {87, 87}, {89, 89}, {119, 87}, {121, 89}, {127, 0},
    {128, 0}, {129, 0}, {136, 0}, {141, 0}, {142, 0}, {143, 0},
    {144, 0}, {150, 45}, {151, 45}, {157, 0}, {158, 0}, {159, 89},
    {160, 9}, {173, 45}, {198, 198}, {199, 199}, {208, 208}, {214, 214},
    {220, 220}, {221, 221}, {230, 198}, {231, 199}, {240, 208}, {246, 214},
    {252, 220}, {253, 221}, {255, 89},
};

static const WEIGHT_CHANGE g_changes_nynorsk[] = {
    {47, 47}, {87, 87}, {89, 89}, {119, 87}, {121, 89}, {127, 0},
    {128, 0}, {129, 0}, {136, 0}, {141, 0}, {142, 0}, {143, 0},
    {144, 0}, {150, 45}, {151, 45}, {157, 0}, {158, 0}, {159, 89},
    {160, 9}, {173, 45}, {196, 196}, {197, 197}, {198, 196}, {214, 214},
    {216, 214}, {220, 89}, {221, 89}, {228, 196}, {229, 197}, {230, 196},
    {246, 214}, {248, 214}, {252, 89}, {253, 89}, {255, 89},
};

static const WEIGHT_CHANGE g_changes_irish_english[] = {
    {47, 47}, {87, 87}, {89, 89}, {119, 87}, {121, 89}, {127, 0},
    {128, 0}, {129, 0}, {136, 0}, {141, 0}, {142, 0}, {143, 0},
    {144, 0}, {150, 45}, {151, 45}, {157, 0}, {158, 0}, {159, 89},
    {160, 9}, {173, 45}, {198, 198}, {208, 208}, {221, 89}, {230, 198},
    {240, 208}, {253, 89}, {255, 89},
};

static const WEIGHT_CHANGE g_changes_arabic[] = {
    {47, 47}, {87, 87}, {89, 89}, {119, 87}, {121, 89}, {128, 128},
    {129, 129}, {136, 94}, {138, 138}, {141, 141}, {142, 142}, {143, 143},
    {144, 144}, {151, 151}, {154, 154}, {157, 157}, {158, 0}, {159, 159},
    {170, 170}, {173, 173}, {186, 186}, {192, 192}, {193, 193}, {194, 194},
    {195, 193}, {196, 193}, {197, 193}, {198, 193}, {199, 199}, {200, 200},
    {201, 201}, {202, 201}, {203, 203}, {204, 204}, {205, 205}, {206, 206},
    {207, 207}, {208, 208}, {209, 209}, {210, 210}, {211, 211}, {212, 212},
    {213, 213}, {214, 214}, {216, 216}, {217, 217}, {218, 218}, {219, 219},
    {220, 0}, {221, 221}, {225, 225}, {227, 227}, {228, 228}, {229, 229},
    {230, 230}, {236, 236}, {237, 236}, {240, 240}, {241, 241}, {242, 242},
    {243, 243}, {245, 245}, {246, 246}, {248, 248}, {250, 250}, {253, 0},
    {254, 0}, {255, 255},
};

static const WEIGHT_CHANGE g_changes_hebrew[] = {
    {47, 47}, {87, 87}, {89, 89}, {119, 87}, {121, 89}, {128, 128},
    {129, 129}, {136, 94}, {138, 138}, {141, 141}, {142, 142}, {143, 143},
    {144, 144}, {151, 151}, {154, 154}, {156, 156}, {157, 157}, {158, 158},
    {159, 159}, {170, 170}, {173, 173}, {186, 186}, {192, 192}, {193, 193},
    {194, 194}, {195, 195}, {196, 196}, {197, 197}, {198, 198}, {199, 199},
    {200, 200}, {201, 201}, {202, 202}, {203, 203}, {204, 204}, {205, 205},
    {206, 206}, {207, 207}, {208, 208}, {209, 209}, {210, 210}, {211, 211},
    {212, 212}, {213, 213}, {214, 214}, {216, 216}, {217, 217}, {218, 218},
    {219, 219}, {220, 220}, {221, 221}, {224, 224}, {225, 225}, {226, 226},
    {227, 227}, {228, 228}, {229, 229}, {230, 230}, {231, 231}, {232, 232},
    {233, 233}, {234, 234}, {235, 234}, {236, 236}, {237, 237}, {238, 237},
    {239, 239}, {240, 239}, {241, 241}, {242, 242}, {243, 243}, {244, 243},
    {245, 245}, {246, 245}, {248, 248}, {249, 249}, {250, 250}, {251, 251},
    {252, 252}, {253, 0}, {254, 0}, {255, 255},
};

static const WEIGHT_CHANGE g_mac_changes_central_european[] = {
    {87, 87}, {89, 89}, {119, 87}, {121, 89}, {130, 65}, {132, 65},
    {137, 67}, {139, 67}, {140, 67}, {143, 90}, {144, 90}, {145, 68},
    {147, 68}, {148, 69}, {149, 69}, {150, 69}, {152, 69}, {157, 69},
    {158, 69}, {162, 69}, {171, 69}, {174, 71}, {175, 73}, {176, 73},
    {177, 73}, {180, 73}, {181, 75}, {184, 76}, {185, 76}, {186, 76},
    {187, 76}, {188, 76}, {189, 76}, {190, 76}, {191, 78}, {192, 78},
    {193, 78}, {196, 78}, {197, 78}, {203, 78}, {204, 79}, {206, 79},
    {207, 79}, {216, 79}, {217, 82}, {218, 82}, {219, 82}, {222, 82},
    {223, 82}, {224, 82}, {225, 83}, {228, 83}, {229, 83}, {230, 83},
    {232, 84}, {233, 84}, {235, 90}, {236, 90}, {237, 85}, {240, 85},
    {241, 85}, {245, 85}, {246, 85}, {247, 85}, {248, 89}, {249, 89},
    {250, 75}, {251, 90}, {252, 76}, {253, 76}, {254, 71}, {255, 255},
};

static const WEIGHT_CHANGE g_mac_changes_russian[] = {
    {87, 87}, {89, 89}, {119, 87}, {121, 89}, {128, 128}, {129, 129},
    {130, 130}, {131, 131}, {132, 132}, {133, 133}, {134, 134}, {135, 135},
    {136, 136}, {137, 137}, {138, 138}, {139, 139}, {140, 140}, {141, 141},
    {142, 142}, {143, 143}, {144, 144}, {145, 145}, {146, 146}, {147, 147},
    {148, 148}, {149, 149}, {150, 150}, {151, 151}, {152, 152}, {153, 153},
    {154, 154}, {155, 155}, {156, 156}, {157, 157}, {158, 158}, {159, 159},
    {172, 171}, {174, 174}, {175, 174}, {180, 167}, {185, 184}, {187, 186},
    {188, 188}, {189, 188}, {190, 190}, {191, 190}, {192, 183}, {203, 203},
    {204, 203}, {205, 205}, {206, 205}, {207, 193}, {216, 216}, {217, 216},
    {219, 218}, {222, 221}, {223, 159}, {224, 128}, {225, 129}, {226, 130},
    {227, 131}, {228, 132}, {229, 133}, {230, 134}, {231, 135}, {232, 136},
    {233, 137}, {234, 138}, {235, 139}, {236, 140}, {237, 141}, {238, 142},
    {239, 143}, {240, 144}, {241, 145}, {242, 146}, {243, 147}, {244, 148},
    {245, 149}, {246, 150}, {247, 151}, {248, 152}, {249, 153}, {250, 154},
    {251, 155}, {252, 156}, {253, 157}, {254, 158}, {255, 159},
};

static const WEIGHT_CHANGE g_mac_changes_greek[] = {
    {47, 47}, {87, 87}, {89, 89}, {119, 87}, {121, 89}, {127, 0},
    {129, 49}, {130, 50}, {132, 51}, {135, 135}, {139, 0}, {140, 140},
    {146, 146}, {147, 147}, {150, 150}, {151, 151}, {152, 152}, {155, 155},
    {156, 63}, {174, 174}, {175, 175}, {185, 171}, {187, 187}, {188, 188},
    {190, 190}, {191, 191}, {192, 176}, {196, 196}, {202, 9}, {203, 189},
    {204, 204}, {205, 176}, {206, 182}, {207, 207}, {208, 45}, {209, 45},
    {215, 184}, {216, 171}, {217, 195}, {218, 189}, {219, 182}, {220, 184},
    {221, 171}, {222, 195}, {223, 191}, {224, 189}, {225, 176}, {226, 181},
    {227, 190}, {228, 162}, {229, 182}, {230, 188}, {231, 161}, {232, 184},
    {233, 171}, {234, 165}, {235, 186}, {236, 164}, {237, 187}, {238, 193},
    {239, 195}, {240, 166}, {241, 191}, {242, 196}, {243, 170}, {244, 198},
    {245, 163}, {246, 191}, {247, 170}, {248, 204}, {249, 189}, {250, 183},
    {251, 171}, {252, 189}, {253, 171}, {254, 189}, {255, 63},
};

static const WEIGHT_CHANGE g_mac_changes_nynorsk[] = {
    {47, 47}, {87, 87}, {89, 89}, {119, 87}, {121, 89}, {127, 0},
    {128, 128}, {129, 129}, {133, 133}, {134, 89}, {138, 128}, {140, 129},
    {154, 133}, {159, 89}, {174, 128}, {175, 133}, {190, 128}, {191, 133},
    {202, 9}, {208, 45}, {209, 45}, {216, 89}, {217, 89}, {246, 0},
    {248, 0}, {255, 0},
};

static const WEIGHT_CHANGE g_mac_changes_irish_english[] = {
    {47, 47}, {87, 87}, {89, 89}, {119, 87}, {121, 89}, {127, 0},
    {174, 174}, {190, 174}, {202, 9}, {208, 45}, {209, 45}, {216, 89},
    {217, 89}, {246, 0}, {248, 0}, {255, 0},
};

static const WEIGHT_CHANGE g_mac_changes_arabic[] = {
    {47, 47}, {87, 87}, {89, 89}, {119, 87}, {121, 89}, {129, 129},
    {139, 139}, {140, 140}, {147, 147}, {152, 152}, {155, 155}, {174, 174},
    {175, 175}, {187, 187}, {188, 188}, {190, 190}, {191, 191}, {195, 193},
    {196, 193}, {197, 193}, {198, 193}, {202, 201}, {203, 203}, {204, 204},
    {205, 205}, {207, 207}, {209, 209}, {216, 216}, {217, 217}, {222, 222},
    {223, 223}, {224, 0}, {229, 229}, {230, 230}, {231, 231}, {232, 232},
    {233, 233}, {234, 233}, {235, 235}, {236, 236}, {237, 237}, {238, 238},
    {239, 239}, {240, 240}, {241, 241}, {242, 242}, {243, 243}, {244, 244},
    {245, 245}, {246, 246}, {248, 248}, {252, 252}, {255, 255},
};

static const WEIGHT_CHANGE g_mac_changes_hebrew[] = {
    {47, 47}, {87, 87}, {89, 89}, {119, 87}, {121, 89}, {174, 174},
    {175, 175}, {187, 187}, {188, 188}, {190, 190}, {191, 191}, {196, 196},
    {203, 203}, {204, 204}, {205, 205}, {207, 207}, {209, 209}, {216, 216},
    {217, 217}, {222, 222}, {223, 223}, {229, 229}, {230, 230}, {231, 231},
    {232, 232}, {233, 233}, {234, 234}, {235, 234}, {236, 236}, {237, 237},
    {238, 237}, {239, 239}, {240, 239}, {241, 241}, {242, 242}, {243, 243},
    {244, 243}, {245, 245}, {246, 245}, {248, 248}, {252, 252}, {255, 255},
};

static const DBCS_WEIGHT g_pairs_japanese[] = {
    {0x81, 0x51, 95, 0}, {0x82, 0x4f, 48, 0}, {0x82, 0x50, 49, 0}, {0x82, 0x51, 50, 0},
    {0x82, 0x52, 51, 0}, {0x82, 0x53, 52, 0}, {0x82, 0x54, 53, 0}, {0x82, 0x55, 54, 0},
    {0x82, 0x56, 55, 0}, {0x82, 0x57, 56, 0}, {0x82, 0x58, 57, 0}, {0x82, 0x60, 65, 0},
    {0x82, 0x61, 66, 0}, {0x82, 0x62, 67, 0}, {0x82, 0x63, 68, 0}, {0x82, 0x64, 69, 0},
    {0x82, 0x65, 70, 0}, {0x82, 0x66, 71, 0}, {0x82, 0x67, 72, 0}, {0x82, 0x68, 73, 0},
    {0x82, 0x69, 74, 0}, {0x82, 0x6a, 75, 0}, {0x82, 0x6b, 76, 0}, {0x82, 0x6c, 77, 0},
    {0x82, 0x6d, 78, 0}, {0x82, 0x6e, 79, 0}, {0x82, 0x6f, 80, 0}, {0x82, 0x70, 81, 0},
    {0x82, 0x71, 82, 0}, {0x82, 0x72, 83, 0}, {0x82, 0x73, 84, 0}, {0x82, 0x74, 85, 0},
    {0x82, 0x75, 86, 0}, {0x82, 0x76, 86, 0}, {0x82, 0x77, 88, 0}, {0x82, 0x78, 85, 0},
    {0x82, 0x79, 90, 0}, {0x82, 0x81, 65, 0}, {0x82, 0x82, 66, 0}, {0x82, 0x83, 67, 0},
    {0x82, 0x84, 68, 0}, {0x82, 0x85, 69, 0}, {0x82, 0x86, 70, 0}, {0x82, 0x87, 71, 0},
    {0x82, 0x88, 72, 0}, {0x82, 0x89, 73, 0}, {0x82, 0x8a, 74, 0}, {0x82, 0x8b, 75, 0},
    {0x82, 0x8c, 76, 0}, {0x82, 0x8d, 77, 0}, {0x82, 0x8e, 78, 0}, {0x82, 0x8f, 79, 0},
    {0x82, 0x90, 80, 0}, {0x82, 0x91, 81, 0}, {0x82, 0x92, 82, 0}, {0x82, 0x93, 83, 0},
    {0x82, 0x94, 84, 0}, {0x82, 0x95, 85, 0}, {0x82, 0x96, 86, 0}, {0x82, 0x97, 86, 0},
    {0x82, 0x98, 88, 0}, {0x82, 0x99, 85, 0}, {0x82, 0x9a, 90, 0}, {0x82, 0x9f, 167, 0},
    {0x82, 0xa0, 177, 0}, {0x82, 0xa1, 168, 0}, {0x82, 0xa2, 50, 0}, {0x82, 0xa3, 169, 0},
    {0x82, 0xa4, 51, 0}, {0x82, 0xa5, 65, 0}, {0x82, 0xa6, 180, 0}, {0x82, 0xa7, 171, 0},
    {0x82, 0xa8, 181, 0}, {0x82, 0xa9, 182, 0}, {0x82, 0xaa, 182, 222}, {0x82, 0xab, 183, 0},
    {0x82, 0xac, 183, 222}, {0x82, 0xad, 184, 0}, {0x82, 0xae, 184, 222}, {0x82, 0xaf, 49, 0},
    {0x82, 0xb0, 49, 222}, {0x82, 0xb1, 79, 0}, {0x82, 0xb2, 79, 222}, {0x82, 0xb3, 187, 0},
    {0x82, 0xb4, 187, 222}, {0x82, 0xb5, 188, 0}, {0x82, 0xb6, 188, 222}, {0x82, 0xb7, 189, 0},
    {0x82, 0xb8, 189, 222}, {0x82, 0xb9, 190, 0}, {0x82, 0xba, 190, 222}, {0x82, 0xbb, 191, 0},
    {0x82, 0xbc, 191, 222}, {0x82, 0xbd, 65, 0}, {0x82, 0xbe, 65, 222}, {0x82, 0xbf, 65, 0},
    {0x82, 0xc0, 65, 222}, {0x82, 0xc1, 175, 0}, {0x82, 0xc2, 65, 0}, {0x82, 0xc3, 65, 222},
    {0x82, 0xc4, 65, 0}, {0x82, 0xc5, 65, 222}, {0x82, 0xc6, 65, 0}, {0x82, 0xc7, 65, 222},
    {0x82, 0xc8, 65, 0}, {0x82, 0xc9, 65, 0}, {0x82, 0xca, 67, 0}, {0x82, 0xcb, 69, 0},
    {0x82, 0xcc, 69, 0}, {0x82, 0xcd, 69, 0}, {0x82, 0xce, 69, 222}, {0x82, 0xcf, 69, 223},
    {0x82, 0xd0, 69, 0}, {0x82, 0xd1, 69, 222}, {0x82, 0xd2, 69, 223}, {0x82, 0xd3, 73, 0},
    {0x82, 0xd4, 73, 222}, {0x82, 0xd5, 73, 223}, {0x82, 0xd6, 73, 0}, {0x82, 0xd7, 73, 222},
    {0x82, 0xd8, 73, 223}, {0x82, 0xd9, 73, 0}, {0x82, 0xda, 73, 222}, {0x82, 0xdb, 73, 223},
    {0x82, 0xdc, 73, 0}, {0x82, 0xdd, 68, 0}, {0x82, 0xde, 78, 0}, {0x82, 0xdf, 79, 0},
    {0x82, 0xe0, 79, 0}, {0x82, 0xe1, 172, 0}, {0x82, 0xe2, 79, 0}, {0x82, 0xe3, 150, 0},
    {0x82, 0xe4, 79, 0}, {0x82, 0xe5, 174, 0}, {0x82, 0xe6, 79, 0}, {0x82, 0xe7, 215, 0},
    {0x82, 0xe8, 79, 0}, {0x82, 0xe9, 85, 0}, {0x82, 0xea, 85, 0}, {0x82, 0xeb, 85, 0},
    {0x82, 0xec, 67, 238}, {0x82, 0xed, 85, 0}, {0x82, 0xee, 67, 238}, {0x82, 0xef, 68, 219},
    {0x82, 0xf0, 166, 0}, {0x82, 0xf1, 85, 0}, {0x83, 0x40, 167, 0}, {0x83, 0x41, 177, 0},
    {0x83, 0x42, 168, 0}, {0x83, 0x43, 50, 0}, {0x83, 0x44, 169, 0}, {0x83, 0x45, 51, 0},
    {0x83, 0x46, 65, 0}, {0x83, 0x47, 180, 0}, {0x83, 0x48, 171, 0}, {0x83, 0x49, 181, 0},
    {0x83, 0x4a, 182, 0}, {0x83, 0x4b, 182, 222}, {0x83, 0x4c, 183, 0}, {0x83, 0x4d, 183, 222},
    {0x83, 0x4e, 184, 0}, {0x83, 0x4f, 184, 222}, {0x83, 0x50, 49, 0}, {0x83, 0x51, 49, 222},
    {0x83, 0x52, 79, 0}, {0x83, 0x53, 79, 222}, {0x83, 0x54, 187, 0}, {0x83, 0x55, 187, 222},
    {0x83, 0x56, 188, 0}, {0x83, 0x57, 188, 222}, {0x83, 0x58, 189, 0}, {0x83, 0x59, 189, 222},
    {0x83, 0x5a, 190, 0}, {0x83, 0x5b, 190, 222}, {0x83, 0x5c, 191, 0}, {0x83, 0x5d, 191, 222},
    {0x83, 0x5e, 65, 0}, {0x83, 0x5f, 65, 222}, {0x83, 0x60, 65, 0}, {0x83, 0x61, 65, 222},
    {0x83, 0x62, 175, 0}, {0x83, 0x63, 65, 0}, {0x83, 0x64, 65, 222}, {0x83, 0x65, 65, 0},
    {0x83, 0x66, 65, 222}, {0x83, 0x67, 65, 0}, {0x83, 0x68, 65, 222}, {0x83, 0x69, 65, 0},
    {0x83, 0x6a, 65, 0}, {0x83, 0x6b, 67, 0}, {0x83, 0x6c, 69, 0}, {0x83, 0x6d, 69, 0},
    {0x83, 0x6e, 69, 0}, {0x83, 0x6f, 69, 222}, {0x83, 0x70, 69, 223}, {0x83, 0x71, 69, 0},
    {0x83, 0x72, 69, 222}, {0x83, 0x73, 69, 223}, {0x83, 0x74, 73, 0}, {0x83, 0x75, 73, 222},
    {0x83, 0x76, 73, 223}, {0x83, 0x77, 73, 0}, {0x83, 0x78, 73, 222}, {0x83, 0x79, 73, 223},
    {0x83, 0x7a, 73, 0}, {0x83, 0x7b, 73, 222}, {0x83, 0x7c, 73, 223}, {0x83, 0x7d, 73, 0},
    {0x83, 0x7e, 68, 0}, {0x83, 0x80, 78, 0}, {0x83, 0x81, 79, 0}, {0x83, 0x82, 79, 0},
    {0x83, 0x83, 172, 0}, {0x83, 0x84, 79, 0}, {0x83, 0x85, 150, 0}, {0x83, 0x86, 79, 0},
    {0x83, 0x87, 174, 0}, {0x83, 0x88, 79, 0}, {0x83, 0x89, 215, 0}, {0x83, 0x8a, 79, 0},
    {0x83, 0x8b, 85, 0}, {0x83, 0x8c, 85, 0}, {0x83, 0x8d, 85, 0}, {0x83, 0x8f, 85, 0},
    {0x83, 0x92, 166, 0}, {0x83, 0x93, 85, 0}, {0x83, 0x94, 51, 222},
};

static const DBCS_WEIGHT g_pairs_fullwidth[] = {
    {0xa3, 0xb0, 48, 0}, {0xa3, 0xb1, 49, 0}, {0xa3, 0xb2, 50, 0}, {0xa3, 0xb3, 51, 0},
    {0xa3, 0xb4, 52, 0}, {0xa3, 0xb5, 53, 0}, {0xa3, 0xb6, 54, 0}, {0xa3, 0xb7, 55, 0},
    {0xa3, 0xb8, 56, 0}, {0xa3, 0xb9, 57, 0}, {0xa3, 0xc1, 65, 0}, {0xa3, 0xc2, 66, 0},
    {0xa3, 0xc3, 67, 0}, {0xa3, 0xc4, 68, 0}, {0xa3, 0xc5, 69, 0}, {0xa3, 0xc6, 70, 0},
    {0xa3, 0xc7, 71, 0}, {0xa3, 0xc8, 72, 0}, {0xa3, 0xc9, 73, 0}, {0xa3, 0xca, 74, 0},
    {0xa3, 0xcb, 75, 0}, {0xa3, 0xcc, 76, 0}, {0xa3, 0xcd, 77, 0}, {0xa3, 0xce, 78, 0},
    {0xa3, 0xcf, 79, 0}, {0xa3, 0xd0, 80, 0}, {0xa3, 0xd1, 81, 0}, {0xa3, 0xd2, 82, 0},
    {0xa3, 0xd3, 83, 0}, {0xa3, 0xd4, 84, 0}, {0xa3, 0xd5, 85, 0}, {0xa3, 0xd6, 86, 0},
    {0xa3, 0xd7, 86, 0}, {0xa3, 0xd8, 88, 0}, {0xa3, 0xd9, 85, 0}, {0xa3, 0xda, 90, 0},
    {0xa3, 0xdf, 95, 0}, {0xa3, 0xe1, 65, 0}, {0xa3, 0xe2, 66, 0}, {0xa3, 0xe3, 67, 0},
    {0xa3, 0xe4, 68, 0}, {0xa3, 0xe5, 69, 0}, {0xa3, 0xe6, 70, 0}, {0xa3, 0xe7, 71, 0},
    {0xa3, 0xe8, 72, 0}, {0xa3, 0xe9, 73, 0}, {0xa3, 0xea, 74, 0}, {0xa3, 0xeb, 75, 0},
    {0xa3, 0xec, 76, 0}, {0xa3, 0xed, 77, 0}, {0xa3, 0xee, 78, 0}, {0xa3, 0xef, 79, 0},
    {0xa3, 0xf0, 80, 0}, {0xa3, 0xf1, 81, 0}, {0xa3, 0xf2, 82, 0}, {0xa3, 0xf3, 83, 0},
    {0xa3, 0xf4, 84, 0}, {0xa3, 0xf5, 85, 0}, {0xa3, 0xf6, 86, 0}, {0xa3, 0xf7, 86, 0},
    {0xa3, 0xf8, 88, 0}, {0xa3, 0xf9, 85, 0}, {0xa3, 0xfa, 90, 0},
};

static const DBCS_WEIGHT g_pairs_big5[] = {
    {0xa1, 0xc5, 95, 0}, {0xa2, 0xaf, 48, 0}, {0xa2, 0xb0, 49, 0}, {0xa2, 0xb1, 50, 0},
    {0xa2, 0xb2, 51, 0}, {0xa2, 0xb3, 52, 0}, {0xa2, 0xb4, 53, 0}, {0xa2, 0xb5, 54, 0},
    {0xa2, 0xb6, 55, 0}, {0xa2, 0xb7, 56, 0}, {0xa2, 0xb8, 57, 0}, {0xa2, 0xcf, 65, 0},
    {0xa2, 0xd0, 66, 0}, {0xa2, 0xd1, 67, 0}, {0xa2, 0xd2, 68, 0}, {0xa2, 0xd3, 69, 0},
    {0xa2, 0xd4, 70, 0}, {0xa2, 0xd5, 71, 0}, {0xa2, 0xd6, 72, 0}, {0xa2, 0xd7, 73, 0},
    {0xa2, 0xd8, 74, 0}, {0xa2, 0xd9, 75, 0}, {0xa2, 0xda, 76, 0}, {0xa2, 0xdb, 77, 0},
    {0xa2, 0xdc, 78, 0}, {0xa2, 0xdd, 79, 0}, {0xa2, 0xde, 80, 0}, {0xa2, 0xdf, 81, 0},
    {0xa2, 0xe0, 82, 0}, {0xa2, 0xe1, 83, 0}, {0xa2, 0xe2, 84, 0}, {0xa2, 0xe3, 85, 0},
    {0xa2, 0xe4, 86, 0}, {0xa2, 0xe5, 86, 0}, {0xa2, 0xe6, 88, 0}, {0xa2, 0xe7, 85, 0},
    {0xa2, 0xe8, 90, 0}, {0xa2, 0xe9, 65, 0}, {0xa2, 0xea, 66, 0}, {0xa2, 0xeb, 67, 0},
    {0xa2, 0xec, 68, 0}, {0xa2, 0xed, 69, 0}, {0xa2, 0xee, 70, 0}, {0xa2, 0xef, 71, 0},
    {0xa2, 0xf0, 72, 0}, {0xa2, 0xf1, 73, 0}, {0xa2, 0xf2, 74, 0}, {0xa2, 0xf3, 75, 0},
    {0xa2, 0xf4, 76, 0}, {0xa2, 0xf5, 77, 0}, {0xa2, 0xf6, 78, 0}, {0xa2, 0xf7, 79, 0},
    {0xa2, 0xf8, 80, 0}, {0xa2, 0xf9, 81, 0}, {0xa2, 0xfa, 82, 0}, {0xa2, 0xfb, 83, 0},
    {0xa2, 0xfc, 84, 0}, {0xa2, 0xfd, 85, 0}, {0xa2, 0xfe, 86, 0}, {0xa3, 0x40, 86, 0},
    {0xa3, 0x41, 88, 0}, {0xa3, 0x42, 85, 0}, {0xa3, 0x43, 90, 0},
};

/* lead byte ranges of the double byte classes. */
typedef enum { LEADS_NONE, LEADS_JAPANESE, LEADS_GB, LEADS_WIDE } LEAD_RANGE;

/* a locale class: its number in the hash and its weights. */
typedef struct {
    BYTE Number;
    BYTE Leads;
    /* the Macintosh uses the windows weights, with the same overrides on the neutral table. */
    BOOL MacLikeWindows;
    USHORT ChangeCount, MacChangeCount, PairCount;
    const WEIGHT_CHANGE *Changes, *MacChanges;
    const DBCS_WEIGHT *Pairs;
} NAME_CLASS;

#define COUNT_OF(a) ((USHORT)(sizeof(a) / sizeof((a)[0])))
#define NEUTRAL_CLASS(number) {number, LEADS_NONE, FALSE, 0, 0, 0, nullptr, nullptr, nullptr}
#define CHANGED_CLASS(number, name) \
    {number, LEADS_NONE, FALSE, COUNT_OF(g_changes_##name), COUNT_OF(g_mac_changes_##name), 0, g_changes_##name, \
     g_mac_changes_##name, nullptr}
#define SAME_ON_MAC_CLASS(number, name) \
    {number, LEADS_NONE, TRUE, COUNT_OF(g_changes_##name), 0, 0, g_changes_##name, nullptr, nullptr}
#define DOUBLE_BYTE_CLASS(number, leads, pairs) {number, leads, TRUE, 0, 0, COUNT_OF(pairs), nullptr, nullptr, pairs}

static const NAME_CLASS g_class_neutral = NEUTRAL_CLASS(1);
static const NAME_CLASS g_class_central_european = CHANGED_CLASS(2, central_european);
static const NAME_CLASS g_class_russian = CHANGED_CLASS(3, russian);
static const NAME_CLASS g_class_japanese = DOUBLE_BYTE_CLASS(4, LEADS_JAPANESE, g_pairs_japanese);
static const NAME_CLASS g_class_korean = DOUBLE_BYTE_CLASS(5, LEADS_WIDE, g_pairs_fullwidth);
static const NAME_CLASS g_class_chinese = DOUBLE_BYTE_CLASS(6, LEADS_GB, g_pairs_fullwidth);
static const NAME_CLASS g_class_big5 = DOUBLE_BYTE_CLASS(7, LEADS_WIDE, g_pairs_big5);
static const NAME_CLASS g_class_greek = CHANGED_CLASS(8, greek);
static const NAME_CLASS g_class_icelandic = SAME_ON_MAC_CLASS(9, icelandic);
static const NAME_CLASS g_class_turkish = SAME_ON_MAC_CLASS(10, turkish);
static const NAME_CLASS g_class_nynorsk = CHANGED_CLASS(11, nynorsk);
static const NAME_CLASS g_class_irish_english = CHANGED_CLASS(12, irish_english);
static const NAME_CLASS g_class_arabic = CHANGED_CLASS(13, arabic);
static const NAME_CLASS g_class_hebrew = CHANGED_CLASS(14, hebrew);

/* by primary language, except a few locales matched only by their exact id. */
static const NAME_CLASS *class_of_locale(LCID lcid)
{
    switch (lcid) {
    case 0x0405:
    case 0x040E:
    case 0x0415:
    case 0x041B: return &g_class_central_european;
    case 0x0408: return &g_class_greek;
    case 0x040D: return &g_class_hebrew;
    case 0x040F: return &g_class_icelandic;
    case 0x0419: return &g_class_russian;
    case 0x041F: return &g_class_turkish;
    case 0x0429: return &g_class_arabic;
    case 0x0814: return &g_class_nynorsk;
    case 0x1809: return &g_class_irish_english;
    default: break;
    }
    if ((lcid & 0xFFFF) == 0x0404 || (lcid & 0xFFFF) == 0x0C04)
        return &g_class_big5;
    if ((lcid & 0xFF) == LANG_ARABIC)
        return &g_class_arabic;
    switch (lcid & 0x3FF) {
    case LANG_CHINESE: return &g_class_chinese;
    case LANG_JAPANESE: return &g_class_japanese;
    case LANG_KOREAN: return &g_class_korean;
    default: return &g_class_neutral;
    }
}

static BOOL is_lead_byte(const NAME_CLASS *cls, BYTE b)
{
    switch (cls->Leads) {
    case LEADS_JAPANESE: return (b >= 0x80 && b <= 0xA0) || b >= 0xE0;
    case LEADS_GB: return b >= 0xA1 && b <= 0xFE;
    case LEADS_WIDE: return b >= 0x81 && b <= 0xFE;
    default: return FALSE;
    }
}

/* a class's byte weights: the base table plus its overrides. */
typedef struct {
    const BYTE *Table;
    const WEIGHT_CHANGE *Changes;
    USHORT ChangeCount;
    const NAME_CLASS *Class;
} WEIGHTS;

static WEIGHTS weights_of(const NAME_CLASS *cls, BOOL mac)
{
    WEIGHTS w = {g_weight_neutral, cls->Changes, cls->ChangeCount, cls};
    if (mac && !cls->MacLikeWindows) {
        w.Table = g_weight_neutral_mac;
        w.Changes = cls->MacChanges;
        w.ChangeCount = cls->MacChangeCount;
    }
    return w;
}

static BYTE byte_weight(const WEIGHTS *weights, BYTE b)
{
    for (USHORT i = 0; i < weights->ChangeCount && weights->Changes[i].Byte <= b; i++)
        if (weights->Changes[i].Byte == b)
            return weights->Changes[i].Weight;
    return weights->Table[b];
}

static ULONG hash_step(ULONG hash, BYTE weight)
{
    return weight + 37 * hash;
}

/* the character's own weights when the class has them, else those of its two bytes. */
static ULONG hash_double_byte(const WEIGHTS *weights, ULONG hash, BYTE lead, BYTE trail)
{
    const NAME_CLASS *cls = weights->Class;
    for (USHORT i = 0; i < cls->PairCount; i++) {
        const DBCS_WEIGHT *pair = &cls->Pairs[i];
        if (pair->Lead == lead && pair->Trail == trail) {
            hash = hash_step(hash, pair->First);
            return pair->Second ? hash_step(hash, pair->Second) : hash;
        }
    }
    return hash_step(hash_step(hash, byte_weight(weights, lead)), byte_weight(weights, trail));
}

WINOLEAUTAPI ULONG WINAPI LHashValOfNameSysA(SYSKIND syskind, LCID lcid, LPCSTR name)
{
    if (!name)
        return 0;
    const NAME_CLASS *cls = class_of_locale(lcid);
    WEIGHTS weights = weights_of(cls, syskind == SYS_MAC);
    ULONG hash = HASH_SEED;
    for (const BYTE *p = (const BYTE *)name; *p; p++) {
        if (is_lead_byte(cls, *p)) {
            /* a lead byte with nothing after it is dropped. */
            if (!p[1])
                break;
            hash = hash_double_byte(&weights, hash, p[0], p[1]);
            p++;
        } else {
            hash = hash_step(hash, byte_weight(&weights, *p));
        }
    }
    return (ULONG)cls->Number << 20 | (syskind == SYS_MAC ? HASH_MAC_BIT : 0) | (hash % HASH_MODULUS & 0xFFFF);
}

WINOLEAUTAPI ULONG WINAPI LHashValOfNameSys(SYSKIND syskind, LCID lcid, const OLECHAR *name)
{
    char ansi[256];
    if (!name)
        return 0;
    memset(ansi, 0, sizeof(ansi));
    WideCharToMultiByte(CP_ACP, 0, name, -1, ansi, sizeof(ansi), nullptr, nullptr);
    ansi[sizeof(ansi) - 1] = 0;
    return LHashValOfNameSysA(syskind, lcid, ansi);
}


/* ------------------------------------------------------------------ ICreateTypeLib2 */

static TL_LIB *lib_of_create(ICreateTypeLib2 *self)
{
    return CONTAINING_RECORD(self, TL_LIB, lpCreateVtbl);
}

static HRESULT WINAPI cl_query(ICreateTypeLib2 *self, REFIID iid, void **out)
{
    return TlLibQuery(lib_of_create(self), iid, out);
}
static ULONG WINAPI cl_addref(ICreateTypeLib2 *self)
{
    return InterlockedIncrement(&lib_of_create(self)->Refs);
}
static ULONG WINAPI cl_release(ICreateTypeLib2 *self)
{
    TL_LIB *lib = lib_of_create(self);
    LONG refs = InterlockedDecrement(&lib->Refs);
    if (!refs)
        TlLibDestroy(lib);
    return refs;
}

static BOOL valid_name(const WCHAR *name)
{
    return name && *name && wcslen(name) < 256;
}

static TL_TYPE *type_named(TL_LIB *lib, const WCHAR *name, const TL_TYPE *except)
{
    for (ULONG i = 0; i < lib->TypeCount; i++) {
        const WCHAR *n = lib->Types[i]->Name;
        if (lib->Types[i] != except && n && !_wcsicmp(n, name))
            return lib->Types[i];
    }
    return nullptr;
}

static HRESULT WINAPI cl_create_type_info(ICreateTypeLib2 *self, LPOLESTR name, TYPEKIND kind, ICreateTypeInfo **out)
{
    TL_LIB *lib = lib_of_create(self);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!valid_name(name) || (int)kind < TKIND_ENUM || kind >= TKIND_MAX)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    EnterCriticalSection(&lib->Lock);
    if (type_named(lib, name, nullptr)) {
        hr = TYPE_E_NAMECONFLICT;
    } else {
        TL_TYPE *t = TlTypeAdd(lib);
        if (!t) {
            hr = E_OUTOFMEMORY;
        } else {
            t->Kind = kind;
            t->Name = TlStrDup(lib, name);
            t->Loaded = TRUE;
            t->Alignment = 0;
            ITypeInfo *info;
            hr = TlGetTypeInfo(lib, t->Index, FALSE, &info);
            if (SUCCEEDED(hr)) {
                *out = (ICreateTypeInfo *)&t->Info->lpCreateVtbl;
                /* the reference TlGetTypeInfo took is the one handed out, it is the same object. */
            }
        }
    }
    LeaveCriticalSection(&lib->Lock);
    return hr;
}

static HRESULT WINAPI cl_set_name(ICreateTypeLib2 *self, LPOLESTR name)
{
    TL_LIB *lib = lib_of_create(self);
    if (!valid_name(name))
        return E_INVALIDARG;
    EnterCriticalSection(&lib->Lock);
    lib->Name = TlStrDup(lib, name);
    LeaveCriticalSection(&lib->Lock);
    return lib->Name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI cl_set_version(ICreateTypeLib2 *self, WORD major, WORD minor)
{
    TL_LIB *lib = lib_of_create(self);
    lib->Major = major;
    lib->Minor = minor;
    return S_OK;
}

static HRESULT WINAPI cl_set_guid(ICreateTypeLib2 *self, REFGUID guid)
{
    TL_LIB *lib = lib_of_create(self);
    if (!guid)
        return E_INVALIDARG;
    lib->Guid = *guid;
    lib->HasGuid = TRUE;
    return S_OK;
}

static HRESULT set_text(TL_LIB *lib, WCHAR **field, LPOLESTR text)
{
    EnterCriticalSection(&lib->Lock);
    *field = text ? TlStrDup(lib, text) : nullptr;
    LeaveCriticalSection(&lib->Lock);
    return text && !*field ? E_OUTOFMEMORY : S_OK;
}

static HRESULT WINAPI cl_set_doc_string(ICreateTypeLib2 *self, LPOLESTR doc)
{
    TL_LIB *lib = lib_of_create(self);
    return set_text(lib, &lib->Doc, doc);
}

static HRESULT WINAPI cl_set_help_file_name(ICreateTypeLib2 *self, LPOLESTR file)
{
    TL_LIB *lib = lib_of_create(self);
    return set_text(lib, &lib->HelpFile, file);
}

static HRESULT WINAPI cl_set_help_context(ICreateTypeLib2 *self, DWORD context)
{
    lib_of_create(self)->HelpContext = context;
    return S_OK;
}

static HRESULT WINAPI cl_set_lcid(ICreateTypeLib2 *self, LCID lcid)
{
    TL_LIB *lib = lib_of_create(self);
    if (lcid != LOCALE_NEUTRAL && !GetLocaleInfoW(lcid, LOCALE_ILANGUAGE, nullptr, 0))
        return E_INVALIDARG;
    lib->Lcid = lcid;
    return S_OK;
}

static HRESULT WINAPI cl_set_lib_flags(ICreateTypeLib2 *self, UINT flags)
{
    if (flags & ~0xFFu)
        return E_INVALIDARG;
    lib_of_create(self)->Flags = (WORD)flags;
    return S_OK;
}

static HRESULT WINAPI cl_delete_type_info(ICreateTypeLib2 *self, LPOLESTR name)
{
    TL_LIB *lib = lib_of_create(self);
    if (!name)
        return E_INVALIDARG;
    HRESULT hr = TYPE_E_ELEMENTNOTFOUND;
    EnterCriticalSection(&lib->Lock);
    TL_TYPE *t = type_named(lib, name, nullptr);
    if (t) {
        /* later types shift up, and numeric references follow them. */
        for (ULONG i = t->Index; i + 1 < lib->TypeCount; i++) {
            lib->Types[i] = lib->Types[i + 1];
            lib->Types[i]->Index = i;
        }
        lib->TypeCount--;
        hr = S_OK;
    }
    LeaveCriticalSection(&lib->Lock);
    return hr;
}

static HRESULT cust_add(TL_LIB *lib, TL_CUST **list, REFGUID guid, VARIANT *value)
{
    if (!guid || !value)
        return E_INVALIDARG;
    switch (value->vt) {
    case VT_I2: case VT_I4: case VT_R4: case VT_R8: case VT_CY: case VT_DATE: case VT_BSTR: case VT_ERROR:
    case VT_BOOL: case VT_I1: case VT_UI1: case VT_UI2: case VT_UI4: case VT_I8: case VT_UI8: case VT_INT:
    case VT_UINT: case VT_DECIMAL:
        break;
    default:
        return DISP_E_BADVARTYPE;
    }
    EnterCriticalSection(&lib->Lock);
    TL_CUST *item = nullptr;
    for (TL_CUST *c = *list; c; c = c->Next)
        if (IsEqualGUID(&c->Guid, guid))
            item = c;
    HRESULT hr = S_OK;
    if (!item) {
        item = TlAlloc(lib, sizeof(*item));
        if (item) {
            item->Next = *list;
            *list = item;
        }
    }
    if (!item) {
        hr = E_OUTOFMEMORY;
    } else {
        item->Guid = *guid;
        hr = TlVariantCopy(lib, &item->Value, value);
    }
    LeaveCriticalSection(&lib->Lock);
    return hr;
}

static HRESULT WINAPI cl_set_cust_data(ICreateTypeLib2 *self, REFGUID guid, VARIANT *value)
{
    TL_LIB *lib = lib_of_create(self);
    return cust_add(lib, &lib->Cust, guid, value);
}

static HRESULT WINAPI cl_set_help_string_context(ICreateTypeLib2 *self, ULONG context)
{
    lib_of_create(self)->HelpStringContext = context;
    return S_OK;
}

static HRESULT WINAPI cl_set_help_string_dll(ICreateTypeLib2 *self, LPOLESTR dll)
{
    TL_LIB *lib = lib_of_create(self);
    return set_text(lib, &lib->HelpStringDll, dll);
}

/* ------------------------------------------------------------------ ICreateTypeInfo2 */

static TYPEINFO *info_of_create(ICreateTypeInfo2 *self)
{
    return CONTAINING_RECORD(self, TYPEINFO, lpCreateVtbl);
}

static HRESULT WINAPI ci_query(ICreateTypeInfo2 *self, REFIID iid, void **out)
{
    return ((ITypeInfo2 *)info_of_create(self))->lpVtbl->QueryInterface((ITypeInfo2 *)info_of_create(self), iid, out);
}
static ULONG WINAPI ci_addref(ICreateTypeInfo2 *self)
{
    ITypeInfo2 *i = (ITypeInfo2 *)info_of_create(self);
    return i->lpVtbl->AddRef(i);
}
static ULONG WINAPI ci_release(ICreateTypeInfo2 *self)
{
    ITypeInfo2 *i = (ITypeInfo2 *)info_of_create(self);
    return i->lpVtbl->Release(i);
}

#define TYPE_OF(self) (info_of_create(self)->Type)

static void touch(TL_TYPE *t)
{
    t->Layouted = FALSE;
}

static HRESULT WINAPI ci_set_guid(ICreateTypeInfo2 *self, REFGUID guid)
{
    TL_TYPE *t = TYPE_OF(self);
    if (!guid)
        return E_INVALIDARG;
    t->Guid = *guid;
    t->HasGuid = TRUE;
    return S_OK;
}

static HRESULT WINAPI ci_set_type_flags(ICreateTypeInfo2 *self, UINT flags)
{
    TL_TYPE *t = TYPE_OF(self);
    if (flags & ~0x7FFFu)
        return E_INVALIDARG;
    t->Flags = (USHORT)flags;
    touch(t);
    /* refresh the objects so a dual interface gets its other half. */
    ITypeInfo *info;
    if (SUCCEEDED(TlGetTypeInfo(t->Lib, t->Index, FALSE, &info)))
        info->lpVtbl->Release(info);
    return S_OK;
}

static HRESULT WINAPI ci_set_doc_string(ICreateTypeInfo2 *self, LPOLESTR doc)
{
    TL_TYPE *t = TYPE_OF(self);
    t->DocOff = 0xFFFFFFFF;
    return set_text(t->Lib, &t->Doc, doc);
}

static HRESULT WINAPI ci_set_help_context(ICreateTypeInfo2 *self, DWORD context)
{
    TYPE_OF(self)->HelpContext = context;
    return S_OK;
}

static HRESULT WINAPI ci_set_version(ICreateTypeInfo2 *self, WORD major, WORD minor)
{
    TL_TYPE *t = TYPE_OF(self);
    t->Major = major;
    t->Minor = minor;
    return S_OK;
}

/* own types get an index reference, types of other libraries an import record. */
static HRESULT WINAPI ci_add_ref_type_info(ICreateTypeInfo2 *self, ITypeInfo *info, HREFTYPE *ref)
{
    TL_TYPE *t = TYPE_OF(self);
    TL_LIB *lib = t->Lib;
    if (!info || !ref)
        return E_INVALIDARG;
    ITypeLib *other;
    UINT index;
    HRESULT hr = info->lpVtbl->GetContainingTypeLib(info, &other, &index);
    if (FAILED(hr))
        return hr;
    if ((TL_LIB *)other == lib) {
        other->lpVtbl->Release(other);
        *ref = TlHrefOfIndex(index);
        return S_OK;
    }
    TLIBATTR *la;
    TYPEATTR *ta;
    hr = other->lpVtbl->GetLibAttr(other, &la);
    if (FAILED(hr)) {
        other->lpVtbl->Release(other);
        return hr;
    }
    hr = info->lpVtbl->GetTypeAttr(info, &ta);
    if (FAILED(hr)) {
        other->lpVtbl->ReleaseTLibAttr(other, la);
        other->lpVtbl->Release(other);
        return hr;
    }
    BOOL by_guid = !IsEqualGUID(&ta->guid, &GUID_NULL);
    EnterCriticalSection(&lib->Lock);
    ULONG found = lib->ImportCount;
    for (ULONG i = 0; i < lib->ImportCount; i++) {
        TL_IMPORT *imp = &lib->Imports[i];
        if (IsEqualGUID(&imp->Lib, &la->guid) && imp->Major == la->wMajorVerNum && imp->Minor == la->wMinorVerNum &&
            imp->ByGuid == by_guid && (by_guid ? IsEqualGUID(&imp->Type, &ta->guid) : imp->Index == index)) {
            found = i;
            break;
        }
    }
    if (found == lib->ImportCount) {
        TL_IMPORT *list = TlAlloc(lib, (SIZE_T)(lib->ImportCount + 1) * sizeof(TL_IMPORT));
        if (!list) {
            hr = E_OUTOFMEMORY;
        } else {
            if (lib->ImportCount)
                memcpy(list, lib->Imports, lib->ImportCount * sizeof(TL_IMPORT));
            TL_IMPORT *imp = &list[lib->ImportCount];
            memset(imp, 0, sizeof(*imp));
            imp->Lib = la->guid;
            imp->Lcid = la->lcid;
            imp->Major = la->wMajorVerNum;
            imp->Minor = la->wMinorVerNum;
            imp->ByGuid = by_guid;
            imp->Type = ta->guid;
            imp->Index = index;
            imp->Kind = ta->typekind;
            BSTR path = nullptr;
            if (((TL_LIB *)other)->lpVtbl == lib->lpVtbl && ((TL_LIB *)other)->Path) {
                const WCHAR *p = ((TL_LIB *)other)->Path;
                const WCHAR *slash = wcsrchr(p, L'\\');
                imp->File = TlStrDup(lib, slash ? slash + 1 : p);
            } else if (SUCCEEDED(QueryPathOfRegTypeLib(&la->guid, la->wMajorVerNum, la->wMinorVerNum, la->lcid, &path))) {
                const WCHAR *slash = wcsrchr(path, L'\\');
                imp->File = TlStrDup(lib, slash ? slash + 1 : path);
            }
            SysFreeString(path);
            lib->Imports = list;
            found = lib->ImportCount++;
        }
    }
    LeaveCriticalSection(&lib->Lock);
    info->lpVtbl->ReleaseTypeAttr(info, ta);
    other->lpVtbl->ReleaseTLibAttr(other, la);
    other->lpVtbl->Release(other);
    if (FAILED(hr))
        return hr;
    *ref = (HREFTYPE)(found * 12) | 1;
    return S_OK;
}

static BOOL variant_type_ok(VARTYPE vt)
{
    return vt <= VT_DECIMAL || (vt >= VT_I1 && vt <= VT_UINT) || vt == VT_HRESULT || vt == VT_PTR || vt == VT_SAFEARRAY ||
           vt == VT_CARRAY || vt == VT_USERDEFINED || vt == VT_LPSTR || vt == VT_LPWSTR || vt == VT_VOID || vt == VT_INT_PTR ||
           vt == VT_UINT_PTR || vt == VT_RECORD;
}

static HRESULT copy_params(TL_LIB *lib, TL_FUNC *f, const FUNCDESC *fd)
{
    f->ParamCount = (USHORT)fd->cParams;
    f->Params = fd->cParams ? TlAlloc(lib, (SIZE_T)fd->cParams * sizeof(TL_PARAM)) : nullptr;
    if (fd->cParams && !f->Params)
        return E_OUTOFMEMORY;
    for (SHORT i = 0; i < fd->cParams; i++) {
        const ELEMDESC *e = &fd->lprgelemdescParam[i];
        if (!variant_type_ok(e->tdesc.vt))
            return E_INVALIDARG;
        TL_PARAM *p = &f->Params[i];
        TlCopyTypeDesc(lib, &p->Type, &e->tdesc);
        p->Flags = e->paramdesc.wParamFlags;
        if ((p->Flags & PARAMFLAG_FHASDEFAULT) && e->paramdesc.pparamdescex) {
            HRESULT hr = TlVariantCopy(lib, &p->Default, &e->paramdesc.pparamdescex->varDefaultValue);
            if (FAILED(hr))
                return hr;
        } else {
            p->Flags &= ~PARAMFLAG_FHASDEFAULT;
        }
    }
    return S_OK;
}

static HRESULT WINAPI ci_add_func_desc(ICreateTypeInfo2 *self, UINT index, FUNCDESC *fd)
{
    TL_TYPE *t = TYPE_OF(self);
    TL_LIB *lib = t->Lib;
    if (!fd)
        return E_INVALIDARG;
    if (t->Kind != TKIND_INTERFACE && t->Kind != TKIND_DISPATCH && t->Kind != TKIND_MODULE)
        return TYPE_E_BADMODULEKIND;
    if (index > t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    if (fd->cParams < 0 || (fd->cParams && !fd->lprgelemdescParam) || !variant_type_ok(fd->elemdescFunc.tdesc.vt))
        return E_INVALIDARG;
    if (fd->invkind != INVOKE_FUNC && fd->invkind != INVOKE_PROPERTYGET && fd->invkind != INVOKE_PROPERTYPUT &&
        fd->invkind != INVOKE_PROPERTYPUTREF)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    EnterCriticalSection(&lib->Lock);
    for (UINT i = 0; i < t->FuncCount; i++)
        if (t->Funcs[i].Id == fd->memid && fd->memid != MEMBERID_NIL &&
            (t->Funcs[i].Invoke == fd->invkind || t->Funcs[i].Invoke == INVOKE_FUNC || fd->invkind == INVOKE_FUNC)) {
            hr = TYPE_E_DUPLICATEID;
            break;
        }
    for (UINT i = 0; i < t->VarCount && SUCCEEDED(hr); i++)
        if (t->Vars[i].Id == fd->memid && fd->memid != MEMBERID_NIL)
            hr = TYPE_E_DUPLICATEID;
    if (SUCCEEDED(hr)) {
        TL_FUNC *list = TlAlloc(lib, (SIZE_T)(t->FuncCount + 1) * sizeof(TL_FUNC));
        if (!list) {
            hr = E_OUTOFMEMORY;
        } else {
            memcpy(list, t->Funcs, index * sizeof(TL_FUNC));
            memcpy(list + index + 1, t->Funcs + index, (t->FuncCount - index) * sizeof(TL_FUNC));
            TL_FUNC *f = &list[index];
            memset(f, 0, sizeof(*f));
            f->Id = fd->memid;
            f->Kind = fd->funckind;
            f->Invoke = fd->invkind;
            f->Conv = fd->callconv;
            f->Flags = fd->wFuncFlags;
            f->VtOffset = fd->oVft;
            f->VtOffsetSet = fd->oVft != 0;
            f->OptCount = fd->cParamsOpt;
            TlCopyTypeDesc(lib, &f->Ret, &fd->elemdescFunc.tdesc);
            hr = copy_params(lib, f, fd);
            if (SUCCEEDED(hr) && fd->cScodes > 0 && fd->lprgscode) {
                f->Scodes = TlAlloc(lib, (SIZE_T)fd->cScodes * sizeof(SCODE));
                if (f->Scodes) {
                    memcpy(f->Scodes, fd->lprgscode, fd->cScodes * sizeof(SCODE));
                    f->ScodeCount = fd->cScodes;
                }
            }
            if (SUCCEEDED(hr)) {
                t->Funcs = list;
                t->FuncCount++;
                touch(t);
            }
        }
    }
    LeaveCriticalSection(&lib->Lock);
    return hr;
}

static HRESULT WINAPI ci_add_impl_type(ICreateTypeInfo2 *self, UINT index, HREFTYPE ref)
{
    TL_TYPE *t = TYPE_OF(self);
    TL_LIB *lib = t->Lib;
    if (t->Kind != TKIND_INTERFACE && t->Kind != TKIND_DISPATCH && t->Kind != TKIND_COCLASS)
        return TYPE_E_WRONGTYPEKIND;
    if (ref == 0xFFFFFFFF || ref == (HREFTYPE)-2)
        return E_INVALIDARG;
    if (t->Kind != TKIND_COCLASS && (index != 0 || t->ImplCount))
        return t->ImplCount ? TYPE_E_ELEMENTNOTFOUND : TYPE_E_ELEMENTNOTFOUND;
    if (index > t->ImplCount)
        return TYPE_E_ELEMENTNOTFOUND;
    ITypeInfo *target;
    HRESULT hr = TlResolveHref(t, ref & ~(HREFTYPE)2, &target);
    if (FAILED(hr))
        return hr;
    TYPEATTR *ta;
    if (SUCCEEDED(target->lpVtbl->GetTypeAttr(target, &ta))) {
        if (IsEqualGUID(&ta->guid, &IID_IDispatch) && lib->DispatchRef == 0xFFFFFFFF)
            lib->DispatchRef = ref & ~(HREFTYPE)2;
        target->lpVtbl->ReleaseTypeAttr(target, ta);
    }
    target->lpVtbl->Release(target);
    EnterCriticalSection(&lib->Lock);
    TL_IMPL *list = TlAlloc(lib, (SIZE_T)(t->ImplCount + 1) * sizeof(TL_IMPL));
    if (!list) {
        LeaveCriticalSection(&lib->Lock);
        return E_OUTOFMEMORY;
    }
    memcpy(list, t->Impls, index * sizeof(TL_IMPL));
    memcpy(list + index + 1, t->Impls + index, (t->ImplCount - index) * sizeof(TL_IMPL));
    memset(&list[index], 0, sizeof(TL_IMPL));
    list[index].Ref = ref & ~(HREFTYPE)2;
    t->Impls = list;
    t->ImplCount++;
    touch(t);
    LeaveCriticalSection(&lib->Lock);
    return S_OK;
}

static HRESULT WINAPI ci_set_impl_type_flags(ICreateTypeInfo2 *self, UINT index, INT flags)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->ImplCount)
        return TYPE_E_ELEMENTNOTFOUND;
    if (flags & ~0xF)
        return E_INVALIDARG;
    t->Impls[index].Flags = flags;
    return S_OK;
}

static HRESULT WINAPI ci_set_alignment(ICreateTypeInfo2 *self, WORD alignment)
{
    TL_TYPE *t = TYPE_OF(self);
    if (!alignment || alignment > 16 || (alignment & (alignment - 1)))
        return E_INVALIDARG;
    t->Pack = alignment;
    t->PackSet = TRUE;
    touch(t);
    return S_OK;
}

static HRESULT WINAPI ci_set_schema(ICreateTypeInfo2 *self, LPOLESTR schema)
{
    TL_TYPE *t = TYPE_OF(self);
    return set_text(t->Lib, &t->Schema, schema);
}

static BOOL variant_const_ok(VARTYPE vt)
{
    switch (vt) {
    case VT_I2: case VT_I4: case VT_R4: case VT_R8: case VT_CY: case VT_DATE: case VT_BSTR: case VT_ERROR: case VT_BOOL:
    case VT_I1: case VT_UI1: case VT_UI2: case VT_UI4: case VT_I8: case VT_UI8: case VT_INT: case VT_UINT: case VT_DECIMAL:
        return TRUE;
    }
    return FALSE;
}

static HRESULT WINAPI ci_add_var_desc(ICreateTypeInfo2 *self, UINT index, VARDESC *vd)
{
    TL_TYPE *t = TYPE_OF(self);
    TL_LIB *lib = t->Lib;
    if (!vd)
        return E_INVALIDARG;
    if (t->Kind == TKIND_INTERFACE || t->Kind == TKIND_COCLASS || t->Kind == TKIND_ALIAS)
        return TYPE_E_BADMODULEKIND;
    if (index > t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    if (!variant_type_ok(vd->elemdescVar.tdesc.vt) || (int)vd->varkind > VAR_DISPATCH)
        return E_INVALIDARG;
    if (vd->varkind == VAR_CONST && (!vd->lpvarValue || !variant_const_ok(vd->lpvarValue->vt)))
        return vd->lpvarValue ? TYPE_E_TYPEMISMATCH : E_INVALIDARG;
    HRESULT hr = S_OK;
    EnterCriticalSection(&lib->Lock);
    for (UINT i = 0; i < t->VarCount; i++)
        if (t->Vars[i].Id == vd->memid && vd->memid != MEMBERID_NIL && t->Kind != TKIND_RECORD && t->Kind != TKIND_UNION)
            hr = TYPE_E_DUPLICATEID;
    for (UINT i = 0; i < t->FuncCount && SUCCEEDED(hr); i++)
        if (t->Funcs[i].Id == vd->memid && vd->memid != MEMBERID_NIL)
            hr = TYPE_E_DUPLICATEID;
    if (SUCCEEDED(hr)) {
        TL_VAR *list = TlAlloc(lib, (SIZE_T)(t->VarCount + 1) * sizeof(TL_VAR));
        if (!list) {
            hr = E_OUTOFMEMORY;
        } else {
            memcpy(list, t->Vars, index * sizeof(TL_VAR));
            memcpy(list + index + 1, t->Vars + index, (t->VarCount - index) * sizeof(TL_VAR));
            TL_VAR *v = &list[index];
            memset(v, 0, sizeof(*v));
            v->Id = vd->memid;
            v->Kind = vd->varkind;
            v->Flags = vd->wVarFlags;
            TlCopyTypeDesc(lib, &v->Type, &vd->elemdescVar.tdesc);
            if (vd->varkind == VAR_CONST)
                hr = TlVariantCopy(lib, &v->Value, vd->lpvarValue);
            else
                v->Offset = vd->oInst;
            if (SUCCEEDED(hr)) {
                t->Vars = list;
                t->VarCount++;
                touch(t);
            }
        }
    }
    LeaveCriticalSection(&lib->Lock);
    return hr;
}

static HRESULT check_names_unique(TL_TYPE *t, UINT skip, const WCHAR *name, INVOKEKIND kind)
{
    for (UINT i = 0; i < t->FuncCount; i++) {
        if (i == skip || !t->Funcs[i].Name || _wcsicmp(t->Funcs[i].Name, name))
            continue;
        /* property accessors may share a name, a function shares it with nothing. */
        if (kind == INVOKE_FUNC || t->Funcs[i].Invoke == INVOKE_FUNC || t->Funcs[i].Invoke == kind)
            return TYPE_E_AMBIGUOUSNAME;
    }
    for (UINT i = 0; i < t->VarCount; i++)
        if (t->Vars[i].Name && !_wcsicmp(t->Vars[i].Name, name))
            return TYPE_E_AMBIGUOUSNAME;
    return S_OK;
}

static HRESULT WINAPI ci_set_func_and_param_names(ICreateTypeInfo2 *self, UINT index, LPOLESTR *names, UINT count)
{
    TL_TYPE *t = TYPE_OF(self);
    TL_LIB *lib = t->Lib;
    if (!names || !count)
        return E_INVALIDARG;
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    TL_FUNC *f = &t->Funcs[index];
    if (count > (UINT)f->ParamCount + 1)
        return TYPE_E_ELEMENTNOTFOUND;
    for (UINT i = 0; i < count; i++)
        if (names[i] && !valid_name(names[i]))
            return E_INVALIDARG;
    HRESULT hr = S_OK;
    if (names[0])
        hr = check_names_unique(t, index, names[0], f->Invoke);
    for (UINT i = 1; i < count && SUCCEEDED(hr); i++)
        for (UINT j = 0; j < i; j++)
            if (names[i] && names[j] && j > 0 && !_wcsicmp(names[i], names[j]))
                hr = TYPE_E_AMBIGUOUSNAME;
    if (FAILED(hr))
        return hr;
    EnterCriticalSection(&lib->Lock);
    if (names[0])
        f->Name = TlStrDup(lib, names[0]);
    for (UINT i = 1; i < count; i++)
        f->Params[i - 1].Name = names[i] ? TlStrDup(lib, names[i]) : nullptr;
    LeaveCriticalSection(&lib->Lock);
    return S_OK;
}

static HRESULT WINAPI ci_set_var_name(ICreateTypeInfo2 *self, UINT index, LPOLESTR name)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    if (!valid_name(name))
        return E_INVALIDARG;
    for (UINT i = 0; i < t->VarCount; i++)
        if (i != index && t->Vars[i].Name && !_wcsicmp(t->Vars[i].Name, name))
            return TYPE_E_AMBIGUOUSNAME;
    for (UINT i = 0; i < t->FuncCount; i++)
        if (t->Funcs[i].Name && !_wcsicmp(t->Funcs[i].Name, name))
            return TYPE_E_AMBIGUOUSNAME;
    EnterCriticalSection(&t->Lib->Lock);
    t->Vars[index].Name = TlStrDup(t->Lib, name);
    LeaveCriticalSection(&t->Lib->Lock);
    return S_OK;
}

static HRESULT WINAPI ci_set_type_desc_alias(ICreateTypeInfo2 *self, TYPEDESC *desc)
{
    TL_TYPE *t = TYPE_OF(self);
    if (t->Kind != TKIND_ALIAS)
        return TYPE_E_WRONGTYPEKIND;
    if (!desc || !variant_type_ok(desc->vt))
        return E_INVALIDARG;
    EnterCriticalSection(&t->Lib->Lock);
    TlCopyTypeDesc(t->Lib, &t->Alias, desc);
    touch(t);
    LeaveCriticalSection(&t->Lib->Lock);
    return S_OK;
}

static HRESULT WINAPI ci_define_func_as_dll_entry(ICreateTypeInfo2 *self, UINT index, LPOLESTR dll, LPOLESTR proc)
{
    TL_TYPE *t = TYPE_OF(self);
    TL_LIB *lib = t->Lib;
    if (t->Kind != TKIND_MODULE)
        return TYPE_E_BADMODULEKIND;
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    if (!dll || !proc)
        return E_INVALIDARG;
    TL_FUNC *f = &t->Funcs[index];
    EnterCriticalSection(&lib->Lock);
    f->DllName = TlStrDup(lib, dll);
    if (proc[0] == L'#') {
        f->HasOrdinal = TRUE;
        f->Ordinal = 0;
        for (const WCHAR *p = proc + 1; *p >= L'0' && *p <= L'9'; p++)
            f->Ordinal = (USHORT)(f->Ordinal * 10 + (*p - L'0'));
        f->EntryName = nullptr;
    } else {
        f->HasOrdinal = FALSE;
        f->EntryName = TlStrDup(lib, proc);
    }
    if (!t->DllName)
        t->DllName = f->DllName;
    LeaveCriticalSection(&lib->Lock);
    return S_OK;
}

static HRESULT WINAPI ci_set_func_doc_string(ICreateTypeInfo2 *self, UINT index, LPOLESTR doc)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return set_text(t->Lib, &t->Funcs[index].Doc, doc);
}

static HRESULT WINAPI ci_set_var_doc_string(ICreateTypeInfo2 *self, UINT index, LPOLESTR doc)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return set_text(t->Lib, &t->Vars[index].Doc, doc);
}

static HRESULT WINAPI ci_set_func_help_context(ICreateTypeInfo2 *self, UINT index, DWORD context)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    t->Funcs[index].HelpContext = context;
    return S_OK;
}

static HRESULT WINAPI ci_set_var_help_context(ICreateTypeInfo2 *self, UINT index, DWORD context)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    t->Vars[index].HelpContext = context;
    return S_OK;
}

static HRESULT WINAPI ci_set_mops(ICreateTypeInfo2 *self, UINT index, BSTR mops)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->FuncCount && index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return S_OK;
}

static HRESULT WINAPI ci_set_type_idldesc(ICreateTypeInfo2 *self, IDLDESC *desc)
{
    if (!desc)
        return E_INVALIDARG;
    TYPE_OF(self)->IdlFlags = desc->wIDLFlags;
    return S_OK;
}

static HRESULT WINAPI ci_lay_out(ICreateTypeInfo2 *self)
{
    TL_TYPE *t = TYPE_OF(self);
    t->Layouted = FALSE;
    return TlEnsureLayout(t);
}

static HRESULT WINAPI ci_delete_func_desc(ICreateTypeInfo2 *self, UINT index)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    memmove(&t->Funcs[index], &t->Funcs[index + 1], (t->FuncCount - index - 1) * sizeof(TL_FUNC));
    t->FuncCount--;
    touch(t);
    return S_OK;
}

static HRESULT WINAPI ci_delete_func_desc_by_memid(ICreateTypeInfo2 *self, MEMBERID id, INVOKEKIND kind)
{
    TL_TYPE *t = TYPE_OF(self);
    for (UINT i = 0; i < t->FuncCount; i++)
        if (t->Funcs[i].Id == id && t->Funcs[i].Invoke == kind)
            return ci_delete_func_desc(self, i);
    return TYPE_E_ELEMENTNOTFOUND;
}

static HRESULT WINAPI ci_delete_var_desc(ICreateTypeInfo2 *self, UINT index)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    memmove(&t->Vars[index], &t->Vars[index + 1], (t->VarCount - index - 1) * sizeof(TL_VAR));
    t->VarCount--;
    touch(t);
    return S_OK;
}

static HRESULT WINAPI ci_delete_var_desc_by_memid(ICreateTypeInfo2 *self, MEMBERID id)
{
    TL_TYPE *t = TYPE_OF(self);
    for (UINT i = 0; i < t->VarCount; i++)
        if (t->Vars[i].Id == id)
            return ci_delete_var_desc(self, i);
    return TYPE_E_ELEMENTNOTFOUND;
}

static HRESULT WINAPI ci_delete_impl_type(ICreateTypeInfo2 *self, UINT index)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->ImplCount)
        return TYPE_E_ELEMENTNOTFOUND;
    memmove(&t->Impls[index], &t->Impls[index + 1], (t->ImplCount - index - 1) * sizeof(TL_IMPL));
    t->ImplCount--;
    touch(t);
    return S_OK;
}

static HRESULT WINAPI ci_set_cust_data(ICreateTypeInfo2 *self, REFGUID guid, VARIANT *value)
{
    TL_TYPE *t = TYPE_OF(self);
    return cust_add(t->Lib, &t->Cust, guid, value);
}

static HRESULT WINAPI ci_set_func_cust_data(ICreateTypeInfo2 *self, UINT index, REFGUID guid, VARIANT *value)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return cust_add(t->Lib, &t->Funcs[index].Cust, guid, value);
}

static HRESULT WINAPI ci_set_param_cust_data(ICreateTypeInfo2 *self, UINT func, UINT param, REFGUID guid,
                                             VARIANT *value)
{
    TL_TYPE *t = TYPE_OF(self);
    if (func >= t->FuncCount || param >= t->Funcs[func].ParamCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return cust_add(t->Lib, &t->Funcs[func].Params[param].Cust, guid, value);
}

static HRESULT WINAPI ci_set_var_cust_data(ICreateTypeInfo2 *self, UINT index, REFGUID guid, VARIANT *value)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return cust_add(t->Lib, &t->Vars[index].Cust, guid, value);
}

static HRESULT WINAPI ci_set_impl_type_cust_data(ICreateTypeInfo2 *self, UINT index, REFGUID guid, VARIANT *value)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->ImplCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return cust_add(t->Lib, &t->Impls[index].Cust, guid, value);
}

static HRESULT WINAPI ci_set_help_string_context(ICreateTypeInfo2 *self, ULONG context)
{
    TYPE_OF(self)->HelpStringContext = context;
    return S_OK;
}

static HRESULT WINAPI ci_set_func_help_string_context(ICreateTypeInfo2 *self, UINT index, ULONG context)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    t->Funcs[index].HelpStringContext = context;
    return S_OK;
}

static HRESULT WINAPI ci_set_var_help_string_context(ICreateTypeInfo2 *self, UINT index, ULONG context)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    t->Vars[index].HelpStringContext = context;
    return S_OK;
}

static HRESULT WINAPI ci_invalidate(ICreateTypeInfo2 *self)
{
    touch(TYPE_OF(self));
    return S_OK;
}

static HRESULT WINAPI ci_set_name(ICreateTypeInfo2 *self, LPOLESTR name)
{
    TL_TYPE *t = TYPE_OF(self);
    if (!valid_name(name))
        return E_INVALIDARG;
    if (type_named(t->Lib, name, t))
        return TYPE_E_NAMECONFLICT;
    t->Name = TlStrDup(t->Lib, name);
    return t->Name ? S_OK : E_OUTOFMEMORY;
}

const ICreateTypeInfo2Vtbl g_createinfo_vtbl = {
    ci_query, ci_addref, ci_release, ci_set_guid, ci_set_type_flags, ci_set_doc_string, ci_set_help_context,
    ci_set_version, ci_add_ref_type_info, ci_add_func_desc, ci_add_impl_type, ci_set_impl_type_flags, ci_set_alignment,
    ci_set_schema, ci_add_var_desc, ci_set_func_and_param_names, ci_set_var_name, ci_set_type_desc_alias,
    ci_define_func_as_dll_entry, ci_set_func_doc_string, ci_set_var_doc_string, ci_set_func_help_context,
    ci_set_var_help_context, ci_set_mops, ci_set_type_idldesc, ci_lay_out, ci_delete_func_desc,
    ci_delete_func_desc_by_memid, ci_delete_var_desc, ci_delete_var_desc_by_memid, ci_delete_impl_type, ci_set_cust_data,
    ci_set_func_cust_data, ci_set_param_cust_data, ci_set_var_cust_data, ci_set_impl_type_cust_data,
    ci_set_help_string_context, ci_set_func_help_string_context, ci_set_var_help_string_context, ci_invalidate,
    ci_set_name};

/* ------------------------------------------------------------------ writing the file */

typedef struct {
    BYTE *Data;
    SIZE_T Length, Capacity;
    BOOL Failed;
} BUF;

static BYTE *buf_add(BUF *b, SIZE_T n)
{
    if (b->Failed)
        return nullptr;
    if (b->Length + n > b->Capacity) {
        SIZE_T capacity = b->Capacity ? b->Capacity * 2 : 4096;
        while (capacity < b->Length + n)
            capacity *= 2;
        BYTE *data = b->Data ? HeapReAlloc(GetProcessHeap(), 0, b->Data, capacity) : AutAlloc(capacity);
        if (!data) {
            b->Failed = TRUE;
            return nullptr;
        }
        b->Data = data;
        b->Capacity = capacity;
    }
    BYTE *p = b->Data + b->Length;
    memset(p, 0, n);
    b->Length += n;
    return p;
}

static void buf_u32(BUF *b, ULONG v)
{
    BYTE *p = buf_add(b, 4);
    if (p)
        memcpy(p, &v, 4);
}

static void buf_u16(BUF *b, USHORT v)
{
    BYTE *p = buf_add(b, 2);
    if (p)
        memcpy(p, &v, 2);
}

static void buf_pad(BUF *b, ULONG boundary, BYTE fill)
{
    while (b->Length % boundary) {
        BYTE *p = buf_add(b, 1);
        if (p)
            *p = fill;
    }
}

static void buf_free(BUF *b)
{
    AutFree(b->Data);
    memset(b, 0, sizeof(*b));
}

static void patch32(BUF *b, SIZE_T at, ULONG v)
{
    if (b->Data && at + 4 <= b->Length)
        memcpy(b->Data + at, &v, 4);
}

static ULONG get32(const BUF *b, SIZE_T at)
{
    ULONG v = 0;
    if (b->Data && at + 4 <= b->Length)
        memcpy(&v, b->Data + at, 4);
    return v;
}

typedef struct {
    ULONG Offset;
    WCHAR *Text;
    ULONG Next;
} NAMEREC;

typedef struct {
    TL_LIB *Lib;
    BUF Types, Members, Guids, Names, Strings, Typdesc, Arrays, CustData, CdGuid, Refs, ImpInfo, ImpFiles;
    ULONG GuidBucket[32];
    ULONG NameBucket[128];
    NAMEREC *NameList;
    ULONG NameCount, NameCapacity;
    ULONG NameChars;
    ULONG *NameHeads;
    HRESULT Status;
} WRITER;

static ULONG name_hash(WRITER *w, const WCHAR *text)
{
    return LHashValOfNameSys(w->Lib->Syskind, w->Lib->Lcid, text) & 0xFFFF;
}

static int to_ansi(WRITER *w, const WCHAR *text, char *out, int size)
{
    int n = WideCharToMultiByte(w->Lib->CodePage, 0, text, -1, out, size, nullptr, nullptr);
    return n > 0 ? n - 1 : 0;
}

static ULONG add_name(WRITER *w, const WCHAR *text, ULONG href)
{
    if (!text)
        return 0xFFFFFFFF;
    ULONG hash = name_hash(w, text);
    ULONG bucket = hash & 0x7F;
    for (ULONG at = w->NameBucket[bucket]; at != 0xFFFFFFFF; at = get32(&w->Names, at + 4)) {
        /* names in a chain are compared case insensitively. */
        for (ULONG i = 0; i < w->NameCount; i++)
            if (w->NameList[i].Offset == at) {
                if (!_wcsicmp(w->NameList[i].Text, text)) {
                    if (href != 0xFFFFFFFF && get32(&w->Names, at) == 0xFFFFFFFF)
                        patch32(&w->Names, at, href);
                    return at;
                }
                break;
            }
    }
    char ansi[1100];
    int length = to_ansi(w, text, ansi, sizeof(ansi));
    if (length > 0x3FF)
        length = 0x3FF;
    ULONG at = (ULONG)w->Names.Length;
    buf_u32(&w->Names, href);
    buf_u32(&w->Names, w->NameBucket[bucket]);
    buf_u32(&w->Names, (hash << 16) | 0x3800 | (ULONG)length);
    BYTE *p = buf_add(&w->Names, length);
    if (p)
        memcpy(p, ansi, length);
    buf_pad(&w->Names, 4, 0x57);
    w->NameBucket[bucket] = at;
    if (w->NameCount == w->NameCapacity) {
        ULONG capacity = w->NameCapacity ? w->NameCapacity * 2 : 64;
        NAMEREC *list = AutAlloc(capacity * sizeof(NAMEREC));
        if (!list) {
            w->Status = E_OUTOFMEMORY;
            return at;
        }
        if (w->NameList) {
            memcpy(list, w->NameList, w->NameCount * sizeof(NAMEREC));
            AutFree(w->NameList);
        }
        w->NameList = list;
        w->NameCapacity = capacity;
    }
    w->NameList[w->NameCount].Offset = at;
    w->NameList[w->NameCount].Text = (WCHAR *)text;
    w->NameCount++;
    w->NameChars += length;
    return at;
}

static ULONG add_string(WRITER *w, const WCHAR *text)
{
    if (!text)
        return 0xFFFFFFFF;
    char ansi[1100];
    int length = to_ansi(w, text, ansi, sizeof(ansi));
    if (length > 0xFFFF)
        length = 0xFFFF;
    /* strings are stored once. */
    for (ULONG at = 0; at < w->Strings.Length;) {
        USHORT l;
        memcpy(&l, w->Strings.Data + at, 2);
        if (l == length && !memcmp(w->Strings.Data + at + 2, ansi, length))
            return at;
        at += (2 + l + 3) & ~3u;
    }
    ULONG at = (ULONG)w->Strings.Length;
    buf_u16(&w->Strings, (USHORT)length);
    BYTE *p = buf_add(&w->Strings, length);
    if (p)
        memcpy(p, ansi, length);
    buf_pad(&w->Strings, 4, 0x57);
    return at;
}

static ULONG guid_hash(const GUID *guid)
{
    const ULONG *d = (const ULONG *)guid;
    ULONG v = d[0] ^ d[1] ^ d[2] ^ d[3];
    return ((v & 0xFFFF) ^ (v >> 16)) % 32;
}

static ULONG add_guid(WRITER *w, const GUID *guid, ULONG href)
{
    ULONG bucket = guid_hash(guid);
    for (ULONG at = w->GuidBucket[bucket]; at != 0xFFFFFFFF; at = get32(&w->Guids, at + 20)) {
        if (!memcmp(w->Guids.Data + at, guid, 16)) {
            if (href != 0xFFFFFFFF && get32(&w->Guids, at + 16) == 0xFFFFFFFF)
                patch32(&w->Guids, at + 16, href);
            return at;
        }
    }
    ULONG at = (ULONG)w->Guids.Length;
    BYTE *p = buf_add(&w->Guids, 16);
    if (p)
        memcpy(p, guid, 16);
    buf_u32(&w->Guids, href);
    buf_u32(&w->Guids, w->GuidBucket[bucket]);
    w->GuidBucket[bucket] = at;
    return at;
}

static ULONG file_href(ULONG href)
{
    if (href == 0xFFFFFFFF)
        return href;
    if (href & 1)
        return href & ~2u;
    ULONG index;
    if (!TlIndexOfHref(href, &index))
        return 0xFFFFFFFF;
    return index * 100;
}

static BOOL simple_vt(VARTYPE vt)
{
    return vt != VT_PTR && vt != VT_SAFEARRAY && vt != VT_CARRAY && vt != VT_USERDEFINED;
}

/* plain types are encoded in the reference itself, others go in the table. */
static ULONG enc_typedesc(WRITER *w, const TYPEDESC *td)
{
    if (simple_vt(td->vt))
        return 0x80000000u | ((ULONG)(0x8000 | td->vt) << 16) | td->vt;
    ULONG data, word = 0x7FFF;
    switch (td->vt) {
    case VT_PTR:
    case VT_SAFEARRAY:
        data = enc_typedesc(w, td->lptdesc);
        if (simple_vt(td->lptdesc->vt))
            word = (td->vt == VT_PTR ? 0x4000 : 0x2000) | td->lptdesc->vt;
        else
            word = td->vt == VT_PTR ? 0x7FFF : 0x7FFE;
        break;
    case VT_CARRAY: {
        const ARRAYDESC *a = td->lpadesc;
        ULONG elem = enc_typedesc(w, &a->tdescElem);
        data = (ULONG)w->Arrays.Length;
        buf_u32(&w->Arrays, elem);
        buf_u16(&w->Arrays, a->cDims);
        buf_u16(&w->Arrays, (USHORT)(a->cDims * 8));
        for (USHORT i = 0; i < a->cDims; i++) {
            buf_u32(&w->Arrays, a->rgbounds[i].cElements);
            buf_u32(&w->Arrays, (ULONG)a->rgbounds[i].lLbound);
        }
        word = 0x7FFE;
        break;
    }
    default:
        data = file_href(td->hreftype & 0xFFFFFF);
        break;
    }
    /* descriptions are stored once. */
    for (ULONG at = 0; at + 8 <= w->Typdesc.Length; at += 8) {
        USHORT v, f;
        ULONG d;
        memcpy(&v, w->Typdesc.Data + at, 2);
        memcpy(&f, w->Typdesc.Data + at + 2, 2);
        memcpy(&d, w->Typdesc.Data + at + 4, 4);
        if (v == td->vt && f == word && d == data)
            return at;
    }
    ULONG at = (ULONG)w->Typdesc.Length;
    buf_u16(&w->Typdesc, td->vt);
    buf_u16(&w->Typdesc, (USHORT)word);
    buf_u32(&w->Typdesc, data);
    return at;
}

/* small constants go inline in the reference, others in the custom data segment. */
static ULONG enc_const(WRITER *w, const VARIANT *v)
{
    switch (v->vt) {
    case VT_I2:
    case VT_UI2:
    case VT_BOOL:
        return 0x80000000u | ((ULONG)v->vt << 26) | v->uiVal;
    case VT_I1:
    case VT_UI1:
        return 0x80000000u | ((ULONG)v->vt << 26) | v->bVal;
    case VT_I4:
    case VT_INT:
        if (v->lVal >= 0 && v->lVal < 0x4000000)
            return 0x80000000u | ((ULONG)v->vt << 26) | (ULONG)v->lVal;
        break;
    case VT_UI4:
    case VT_UINT:
    case VT_ERROR:
        if (v->ulVal < 0x4000000)
            return 0x80000000u | ((ULONG)v->vt << 26) | v->ulVal;
        break;
    default:
        break;
    }
    ULONG at = (ULONG)w->CustData.Length;
    switch (v->vt) {
    case VT_I1:
    case VT_UI1:
    case VT_I2:
    case VT_UI2:
    case VT_BOOL: {
        /* always encoded inline above. */
        break;
    }
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
    case VT_R4:
    case VT_ERROR:
        buf_u16(&w->CustData, v->vt);
        buf_u32(&w->CustData, v->ulVal);
        break;
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_I8:
    case VT_UI8: {
        buf_u16(&w->CustData, v->vt);
        BYTE *p = buf_add(&w->CustData, 8);
        if (p)
            memcpy(p, &v->llVal, 8);
        break;
    }
    case VT_DECIMAL: {
        DECIMAL d = v->decVal;
        d.wReserved = VT_DECIMAL;
        BYTE *p = buf_add(&w->CustData, 16);
        if (p)
            memcpy(p, &d, 16);
        break;
    }
    case VT_BSTR: {
        buf_u16(&w->CustData, VT_BSTR);
        if (!v->bstrVal) {
            buf_u32(&w->CustData, 0xFFFFFFFF);
        } else {
            char ansi[2048];
            int n = WideCharToMultiByte(w->Lib->CodePage, 0, v->bstrVal, SysStringLen(v->bstrVal), ansi, sizeof(ansi),
                                        nullptr, nullptr);
            buf_u32(&w->CustData, n);
            BYTE *p = buf_add(&w->CustData, n);
            if (p)
                memcpy(p, ansi, n);
        }
        break;
    }
    default:
        break;
    }
    buf_pad(&w->CustData, 4, 0x57);
    return at;
}

static ULONG enc_cust(WRITER *w, TL_CUST *list)
{
    ULONG head = 0xFFFFFFFF;
    /* written back to front so each item knows its successor. */
    TL_CUST *items[64];
    int n = 0;
    for (TL_CUST *c = list; c && n < 64; c = c->Next)
        items[n++] = c;
    for (int i = n - 1; i >= 0; i--) {
        ULONG guid = add_guid(w, &items[i]->Guid, 0xFFFFFFFF);
        ULONG value = enc_const(w, &items[i]->Value);
        ULONG at = (ULONG)w->CdGuid.Length;
        buf_u32(&w->CdGuid, guid);
        buf_u32(&w->CdGuid, value);
        buf_u32(&w->CdGuid, head);
        head = at;
    }
    return head;
}

static ULONG funcdesc_size(WRITER *w, const TL_FUNC *f)
{
    SYSKIND k = w->Lib->Syskind;
    ULONG fd = k == SYS_WIN64 ? 88 : 52, ed = k == SYS_WIN64 ? 32 : 16, td = k == SYS_WIN64 ? 16 : 8;
    ULONG ex = k == SYS_WIN64 ? 32 : 24;
    ULONG size = fd + f->ParamCount * ed;
    TYPEDESC stack[1];
    (void)stack;
    for (USHORT i = 0; i < f->ParamCount; i++) {
        const TYPEDESC *t = &f->Params[i].Type;
        while (t->vt == VT_PTR || t->vt == VT_SAFEARRAY) {
            size += td;
            t = t->lptdesc;
        }
        if (f->Params[i].Flags & PARAMFLAG_FHASDEFAULT)
            size += ex;
    }
    const TYPEDESC *t = &f->Ret;
    while (t->vt == VT_PTR || t->vt == VT_SAFEARRAY) {
        size += td;
        t = t->lptdesc;
    }
    return size;
}

/*
 * the get, put and putref functions of one member id are chained in the file: each names the next in descending
 * invoke kind order, the lowest names the highest, and a lone function names itself. Readers find the others this way.
 */
static ULONG chained_function(const TL_TYPE *t, ULONG k)
{
    const TL_FUNC *f = &t->Funcs[k];
    ULONG next = k, highest = k;
    for (ULONG j = 0; j < t->FuncCount; j++) {
        const TL_FUNC *g = &t->Funcs[j];
        if (j == k || g->Id != f->Id)
            continue;
        if (g->Invoke > t->Funcs[highest].Invoke)
            highest = j;
        if (g->Invoke < f->Invoke && (next == k || g->Invoke > t->Funcs[next].Invoke))
            next = j;
    }
    return next != k ? next : highest;
}

static void write_members(WRITER *w, TL_TYPE *t, ULONG *memoff)
{
    ULONG cf = t->FuncCount, cv = t->VarCount, total = cf + cv;
    *memoff = (ULONG)w->Members.Length;
    if (!total)
        return;
    BUF recs = {0};
    ULONG *offsets = AutAllocZero(total * 3 * sizeof(ULONG));
    if (!offsets) {
        w->Status = E_OUTOFMEMORY;
        return;
    }
    ULONG *memids = offsets, *names = offsets + total, *where = offsets + 2 * total;
    for (ULONG k = 0; k < total; k++) {
        where[k] = (ULONG)recs.Length;
        if (k < cf) {
            TL_FUNC *f = &t->Funcs[k];
            memids[k] = (ULONG)f->Id;
            names[k] = add_name(w, f->Name, 0xFFFFFFFF);
            BOOL defaults = FALSE, cust = f->Cust != nullptr;
            ULONG hidden = 0;
            for (USHORT j = 0; j < f->ParamCount; j++) {
                if (f->Params[j].Flags & PARAMFLAG_FHASDEFAULT)
                    defaults = TRUE;
                if (f->Params[j].Flags & (PARAMFLAG_FLCID | PARAMFLAG_FRETVAL))
                    hidden++;
                if (f->Params[j].Cust)
                    cust = TRUE;
            }
            ULONG nargs = f->ParamCount;
            ULONG size = 24 + 24 + (cust ? 4 * (nargs + 2) : 0) + nargs * (defaults ? 16 : 12);
            ULONG fk = (ULONG)f->Kind | ((ULONG)f->Invoke << 3) | (cust ? 0x80 : 0) | ((ULONG)f->Conv << 8) |
                       (defaults ? 0x1000 : 0) | (f->HasOrdinal ? 0x2000 : 0) | ((hidden > 3 ? 3 : hidden) << 14) | (chained_function(t, k) << 16);
            buf_u32(&recs, size | (k << 16));
            buf_u32(&recs, enc_typedesc(w, &f->Ret));
            buf_u32(&recs, f->Flags);
            buf_u32(&recs, (USHORT)f->VtOffset | (funcdesc_size(w, f) << 16));
            buf_u32(&recs, fk);
            buf_u32(&recs, nargs | ((ULONG)(USHORT)f->OptCount << 16));
            buf_u32(&recs, f->HelpContext);
            buf_u32(&recs, add_string(w, f->Doc));
            buf_u32(&recs, f->HasOrdinal ? f->Ordinal : add_string(w, f->EntryName));
            buf_u32(&recs, add_string(w, f->HelpFile));
            buf_u32(&recs, add_string(w, f->DllName));
            buf_u32(&recs, f->HelpStringContext);
            if (cust) {
                buf_u32(&recs, enc_cust(w, f->Cust));
                for (ULONG j = 0; j < nargs; j++)
                    buf_u32(&recs, enc_cust(w, f->Params[j].Cust));
                buf_u32(&recs, 0xFFFFFFFF);
            }
            if (defaults)
                for (ULONG j = 0; j < nargs; j++)
                    buf_u32(&recs, (f->Params[j].Flags & PARAMFLAG_FHASDEFAULT) ? enc_const(w, &f->Params[j].Default) : 0xFFFFFFFF);
            for (ULONG j = 0; j < nargs; j++) {
                buf_u32(&recs, enc_typedesc(w, &f->Params[j].Type));
                buf_u32(&recs, add_name(w, f->Params[j].Name, 0xFFFFFFFF));
                buf_u32(&recs, f->Params[j].Flags);
            }
        } else {
            TL_VAR *v = &t->Vars[k - cf];
            memids[k] = (ULONG)v->Id;
            names[k] = add_name(w, v->Name, 0xFFFFFFFF);
            SYSKIND sk = w->Lib->Syskind;
            ULONG vd = (sk == SYS_WIN64 ? 64 : 36) + (sk == SYS_WIN64 ? 24 : 16) * 0;
            buf_u32(&recs, 44 | ((k - cf) << 16));
            buf_u32(&recs, enc_typedesc(w, &v->Type));
            buf_u32(&recs, v->Flags);
            buf_u32(&recs, (USHORT)v->Kind | (vd << 16));
            buf_u32(&recs, v->Kind == VAR_CONST ? enc_const(w, &v->Value) : v->Offset);
            buf_u32(&recs, v->HelpContext);
            buf_u32(&recs, add_string(w, v->Doc));
            buf_u32(&recs, 0xFFFFFFFF);
            buf_u32(&recs, enc_cust(w, v->Cust));
            buf_u32(&recs, v->HelpStringContext);
            buf_u32(&recs, 0xFFFFFFFF);
        }
    }
    buf_u32(&w->Members, (ULONG)recs.Length);
    BYTE *p = buf_add(&w->Members, recs.Length);
    if (p && recs.Data)
        memcpy(p, recs.Data, recs.Length);
    p = buf_add(&w->Members, total * 12);
    if (p)
        memcpy(p, offsets, total * 12);
    buf_free(&recs);
    AutFree(offsets);
}

static ULONG impfile_for(WRITER *w, TL_LIB *lib, const TL_IMPORT *imp, BUF *files, ULONG *cache_keys, ULONG *cache_values,
                         ULONG *cache_count)
{
    for (ULONG i = 0; i < *cache_count; i++) {
        const TL_IMPORT *other = &lib->Imports[cache_keys[i]];
        if (IsEqualGUID(&other->Lib, &imp->Lib) && other->Major == imp->Major && other->Minor == imp->Minor &&
            other->Lcid == imp->Lcid)
            return cache_values[i];
    }
    ULONG at = (ULONG)files->Length;
    ULONG guid = add_guid(w, &imp->Lib, 0xFFFFFFFF);
    buf_u32(files, guid);
    buf_u32(files, imp->Lcid);
    buf_u32(files, imp->Major | ((ULONG)imp->Minor << 16));
    char ansi[300];
    int n = imp->File ? to_ansi(w, imp->File, ansi, sizeof(ansi)) : 0;
    buf_u16(files, (USHORT)((n << 2) | 3));
    BYTE *p = buf_add(files, n);
    if (p && n)
        memcpy(p, ansi, n);
    buf_pad(files, 4, 0x57);
    cache_keys[*cache_count] = (ULONG)(imp - lib->Imports);
    cache_values[*cache_count] = at;
    (*cache_count)++;
    return at;
}

HRESULT TlSaveMsft(TL_LIB *lib, LPCOLESTR path)
{
    WRITER w;
    memset(&w, 0, sizeof(w));
    w.Lib = lib;
    w.Status = S_OK;
    memset(w.GuidBucket, 0xFF, sizeof(w.GuidBucket));
    memset(w.NameBucket, 0xFF, sizeof(w.NameBucket));
    HRESULT hr = S_OK;
    EnterCriticalSection(&lib->Lock);
    for (ULONG i = 0; i < lib->TypeCount && SUCCEEDED(hr); i++) {
        hr = TlLoadMembers(lib->Types[i]);
        if (SUCCEEDED(hr))
            hr = TlEnsureLayout(lib->Types[i]);
    }
    if (FAILED(hr))
        goto done;
    {
        ULONG count = lib->TypeCount;
        ULONG lib_name = add_name(&w, lib->Name, 0xFFFFFFFF);
        ULONG lib_guid = lib->HasGuid ? add_guid(&w, &lib->Guid, 0xFFFFFFFF) : 0xFFFFFFFF;
        ULONG lib_doc = add_string(&w, lib->Doc);
        ULONG lib_help = add_string(&w, lib->HelpFile);
        ULONG lib_help_dll = add_string(&w, lib->HelpStringDll);
        /* type names go into the name table before anything refers to them. */
        for (ULONG i = 0; i < count; i++)
            add_name(&w, lib->Types[i]->Name, i * 100);
        ULONG *memoffs = AutAllocZero((count + 1) * sizeof(ULONG));
        if (!memoffs) {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        for (ULONG i = 0; i < count; i++) {
            TL_TYPE *t = lib->Types[i];
            ULONG memoff;
            write_members(&w, t, &memoff);
            memoffs[i] = memoff;
            ULONG guid = t->HasGuid ? add_guid(&w, &t->Guid, i * 100) : 0xFFFFFFFF;
            ULONG d1 = 0xFFFFFFFF, d2 = 0xFFFFFFFF;
            ULONG impl_count = t->ImplCount;
            if (t->Kind == TKIND_COCLASS) {
                ULONG chain = 0xFFFFFFFF;
                for (int j = (int)t->ImplCount - 1; j >= 0; j--) {
                    ULONG at = (ULONG)w.Refs.Length;
                    buf_u32(&w.Refs, file_href(t->Impls[j].Ref));
                    buf_u32(&w.Refs, t->Impls[j].Flags);
                    buf_u32(&w.Refs, enc_cust(&w, t->Impls[j].Cust));
                    buf_u32(&w.Refs, chain);
                    chain = at;
                }
                d1 = chain;
            } else if (t->Kind == TKIND_INTERFACE || t->Kind == TKIND_DISPATCH) {
                if (t->ImplCount)
                    d1 = file_href(t->Impls[0].Ref);
                d2 = ((ULONG)t->InheritedFuncs << 16) | t->InheritedImpls;
            } else if (t->Kind == TKIND_ALIAS) {
                d1 = enc_typedesc(&w, &t->Alias);
            } else if (t->Kind == TKIND_MODULE) {
                d1 = add_string(&w, t->DllName);
            }
            ULONG kind_word = (i << 16) | ((ULONG)(t->Alignment & 0x1F) << 11) | 0x220 | (ULONG)t->Kind |
                              ((t->Flags & TYPEFLAG_FDUAL) ? 0x10 : 0);
            buf_u32(&w.Types, kind_word);
            buf_u32(&w.Types, memoff);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, 3);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, (ULONG)t->FuncCount | ((ULONG)t->VarCount << 16));
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, guid);
            buf_u32(&w.Types, t->Flags);
            buf_u32(&w.Types, add_name(&w, t->Name, i * 100));
            buf_u32(&w.Types, t->Major | ((ULONG)t->Minor << 16));
            buf_u32(&w.Types, add_string(&w, t->Doc));
            buf_u32(&w.Types, t->HelpStringContext);
            buf_u32(&w.Types, t->HelpContext);
            buf_u32(&w.Types, enc_cust(&w, t->Cust));
            buf_u32(&w.Types, impl_count | ((ULONG)t->SizeVft << 16));
            buf_u32(&w.Types, t->SizeInstance);
            buf_u32(&w.Types, d1);
            buf_u32(&w.Types, d2);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, 0xFFFFFFFF);
        }
        ULONG *keys = AutAllocZero((lib->ImportCount + 1) * 2 * sizeof(ULONG) + sizeof(ULONG));
        ULONG *values = keys + lib->ImportCount + 1, cached = 0;
        for (ULONG i = 0; i < lib->ImportCount; i++) {
            TL_IMPORT *imp = &lib->Imports[i];
            ULONG file = keys ? impfile_for(&w, lib, imp, &w.ImpFiles, keys, values, &cached) : 0;
            buf_u32(&w.ImpInfo, (i & 0xFFFF) | ((ULONG)(imp->ByGuid ? 1 : 0) << 16) | ((ULONG)imp->Kind << 24));
            buf_u32(&w.ImpInfo, file);
            buf_u32(&w.ImpInfo, imp->ByGuid ? add_guid(&w, &imp->Type, 0xFFFFFFFF) : imp->Index);
        }
        AutFree(keys);
        ULONG custom = enc_cust(&w, lib->Cust);

        /* file layout: header, type table, segment directory, segments, members. */
        BUF out = {0};
        ULONG varflags = (ULONG)(lib->Syskind & 3) | 0x40 | (lib_help_dll != 0xFFFFFFFF ? 0x100 : 0);
        ULONG pos_header = 0x54 + (lib_help_dll != 0xFFFFFFFF ? 4 : 0) + count * 4 + 240;
        struct {
            BUF *Buf;
            int Seg;
        } order[] = {{&w.Types, SEG_TYPEINFO}, {&w.ImpInfo, SEG_IMPINFO}, {&w.ImpFiles, SEG_IMPFILES}, {&w.Refs, SEG_REFTAB},
                     {nullptr, SEG_GUIDHASH}, {&w.Guids, SEG_GUID}, {nullptr, SEG_NAMEHASH}, {&w.Names, SEG_NAME},
                     {&w.Strings, SEG_STRING}, {&w.Typdesc, SEG_TYPDESC}, {&w.Arrays, SEG_ARRAYDESC},
                     {&w.CustData, SEG_CUSTDATA}, {&w.CdGuid, SEG_CDGUID}};
        ULONG seg_offset[15], seg_length[15];
        ULONG at = pos_header;
        for (int i = 0; i < 15; i++) {
            seg_offset[i] = 0xFFFFFFFF;
            seg_length[i] = 0;
        }
        for (int i = 0; i < 13; i++) {
            ULONG length = order[i].Buf ? (ULONG)order[i].Buf->Length : order[i].Seg == SEG_GUIDHASH ? 128 : 512;
            if (!length)
                continue;
            seg_offset[order[i].Seg] = at;
            seg_length[order[i].Seg] = length;
            at += length;
        }
        ULONG members_base = at;
        buf_u32(&out, 0x5446534D);
        buf_u32(&out, 0x00010002);
        buf_u32(&out, lib_guid);
        buf_u32(&out, lib->Lcid);
        buf_u32(&out, lib->Lcid);
        buf_u32(&out, varflags);
        buf_u32(&out, lib->Major | ((ULONG)lib->Minor << 16));
        buf_u32(&out, lib->Flags);
        buf_u32(&out, count);
        buf_u32(&out, lib_doc);
        buf_u32(&out, lib->HelpStringContext);
        buf_u32(&out, lib->HelpContext);
        buf_u32(&out, w.NameCount);
        buf_u32(&out, w.NameChars);
        buf_u32(&out, lib_name);
        buf_u32(&out, lib_help);
        buf_u32(&out, custom);
        buf_u32(&out, 32);
        buf_u32(&out, 128);
        buf_u32(&out, lib->DispatchRef == 0xFFFFFFFF ? 0xFFFFFFFF : file_href(lib->DispatchRef));
        buf_u32(&out, lib->ImportCount);
        if (lib_help_dll != 0xFFFFFFFF)
            buf_u32(&out, lib_help_dll);
        for (ULONG i = 0; i < count; i++)
            buf_u32(&out, i * 100);
        for (int i = 0; i < 15; i++) {
            buf_u32(&out, seg_offset[i]);
            buf_u32(&out, seg_length[i]);
            buf_u32(&out, 0xFFFFFFFF);
            buf_u32(&out, 0xF);
        }
        for (int i = 0; i < 13; i++) {
            if (order[i].Buf) {
                if (i == 0) {
                    /* type members start where the segments end. */
                    for (ULONG k = 0; k < count; k++)
                        patch32(&w.Types, k * 100 + 4, members_base + memoffs[k]);
                }
                BYTE *p = buf_add(&out, order[i].Buf->Length);
                if (p && order[i].Buf->Length)
                    memcpy(p, order[i].Buf->Data, order[i].Buf->Length);
            } else if (order[i].Seg == SEG_GUIDHASH) {
                BYTE *p = buf_add(&out, 128);
                if (p)
                    memcpy(p, w.GuidBucket, 128);
            } else {
                BYTE *p = buf_add(&out, 512);
                if (p)
                    memcpy(p, w.NameBucket, 512);
            }
        }
        BYTE *p = buf_add(&out, w.Members.Length);
        if (p && w.Members.Length)
            memcpy(p, w.Members.Data, w.Members.Length);
        AutFree(memoffs);
        if (out.Failed || w.Status != S_OK) {
            hr = FAILED(w.Status) ? w.Status : E_OUTOFMEMORY;
        } else {
            HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                hr = STG_E_ACCESSDENIED;
            } else {
                DWORD written;
                if (!WriteFile(file, out.Data, (DWORD)out.Length, &written, nullptr) || written != out.Length)
                    hr = HRESULT_WRITEFAULT;
                CloseHandle(file);
            }
        }
        buf_free(&out);
    }
done:
    LeaveCriticalSection(&lib->Lock);
    buf_free(&w.Types);
    buf_free(&w.Members);
    buf_free(&w.Guids);
    buf_free(&w.Names);
    buf_free(&w.Strings);
    buf_free(&w.Typdesc);
    buf_free(&w.Arrays);
    buf_free(&w.CustData);
    buf_free(&w.CdGuid);
    buf_free(&w.Refs);
    buf_free(&w.ImpInfo);
    buf_free(&w.ImpFiles);
    AutFree(w.NameList);
    return hr;
}

static HRESULT WINAPI cl_save_all_changes(ICreateTypeLib2 *self)
{
    TL_LIB *lib = lib_of_create(self);
    if (!lib->Path)
        return E_INVALIDARG;
    if (!lib->HasGuid)
        return E_UNEXPECTED;
    return TlSaveMsft(lib, lib->Path);
}

const ICreateTypeLib2Vtbl g_createlib_vtbl = {
    cl_query, cl_addref, cl_release, cl_create_type_info, cl_set_name, cl_set_version, cl_set_guid, cl_set_doc_string,
    cl_set_help_file_name, cl_set_help_context, cl_set_lcid, cl_set_lib_flags, cl_save_all_changes, cl_delete_type_info,
    cl_set_cust_data, cl_set_help_string_context, cl_set_help_string_dll};

WINOLEAUTAPI HRESULT WINAPI CreateTypeLib2(SYSKIND kind, LPCOLESTR file, ICreateTypeLib2 **out)
{
    if (!file || !out)
        return E_INVALIDARG;
    *out = nullptr;
    if (kind != SYS_WIN32 && kind != SYS_WIN64 && (int)kind != -1)
        return E_INVALIDARG;
    if ((int)kind == -1)
        kind = sizeof(void *) == 8 ? SYS_WIN64 : SYS_WIN32;
    TL_LIB *lib = TlLibCreate();
    if (!lib)
        return E_OUTOFMEMORY;
    lib->Writable = TRUE;
    lib->Syskind = lib->FileSyskind = kind;
    lib->Lcid = GetUserDefaultLCID();
    if (!lib->Lcid)
        lib->Lcid = 0x409;
    lib->CodePage = CP_ACP;
    WCHAR full[MAX_PATH];
    DWORD n = GetFullPathNameW(file, MAX_PATH, full, nullptr);
    lib->Path = TlStrDup(lib, n && n < MAX_PATH ? full : file);
    if (!lib->Path) {
        TlLibDestroy(lib);
        return E_OUTOFMEMORY;
    }
    *out = (ICreateTypeLib2 *)&lib->lpCreateVtbl;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI OACreateTypeLib2(SYSKIND kind, LPCOLESTR file, ICreateTypeLib2 **out)
{
    return CreateTypeLib2(kind, file, out);
}

WINOLEAUTAPI HRESULT WINAPI CreateTypeLib(SYSKIND kind, LPCOLESTR file, ICreateTypeLib **out)
{
    return CreateTypeLib2(kind, file, (ICreateTypeLib2 **)out);
}
