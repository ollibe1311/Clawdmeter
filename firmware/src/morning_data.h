#pragma once
#include <stdint.h>

#define MAX_NAME_LEN 32
#define MAX_TEXT_LEN 128
#define MAX_ITEMS 20
#define MAX_ACTIVITIES 10
#define MAX_BIRTHDAYS 5

enum ItemType {
    ITEM_EVENT,
    ITEM_BRING,
    ITEM_ACTION,
    ITEM_UNIFORM,
    ITEM_ACTIVITY,
    ITEM_OTHER
};

enum UniformType {
    UNIFORM_NONE,
    UNIFORM_SPORT,
    UNIFORM_FORMAL,
    UNIFORM_MUFTI
};

enum Activity {
    ACTIVITY_FOOTBALL,
    ACTIVITY_SAILING,
    ACTIVITY_DANCE,
    ACTIVITY_CHOIR,
    ACTIVITY_SWIMMING,
    ACTIVITY_PARTY,
    ACTIVITY_TROMBONE,
    ACTIVITY_VIOLIN,
    ACTIVITY_PIANO,
    ACTIVITY_ART,
    ACTIVITY_POTTERY,
    ACTIVITY_SURFING,
    ACTIVITY_NETBALL,
    ACTIVITY_OTHER
};

struct Item {
    char text[MAX_TEXT_LEN];
    ItemType type;
    char deadline[16];  // YYYY-MM-DD or empty
};

struct KidDay {
    UniformType uniform;
    Activity activities[MAX_ACTIVITIES];
    int activity_count;
    char tonight[MAX_ITEMS][MAX_TEXT_LEN];
    int tonight_count;
    Item items[MAX_ITEMS];
    int item_count;
};

struct ParentDay {
    Item items[MAX_ITEMS];
    int item_count;
};

struct DayData {
    char date[16];  // YYYY-MM-DD
    char birthdays[MAX_BIRTHDAYS][MAX_NAME_LEN];
    int birthday_count;
    KidDay arlo;
    KidDay willow;
    ParentDay parents;
};

struct FeedData {
    int version;
    char generated[32];  // ISO8601+11:00
    DayData days[7];
    int day_count;
    bool valid;
    uint32_t fetched_at;  // millis() when last fetched
};

struct WeatherData {
    float max_temp[7];
    float min_temp[7];
    int precip_prob[7];  // percentage
    bool valid;
    uint32_t fetched_at;  // millis() when last fetched
};

struct DressRules {
    float shorts_threshold;     // >= this temp → shorts/skort
    float pants_threshold;      // < this temp → long sleeves
    int rain_threshold;         // >= this precip prob → rain jacket
};

// Time windows when screen auto-powers on (local time)
struct ScreenSchedule {
    int morning_start_hour;    // 06:00
    int morning_start_min;
    int morning_end_hour;      // 09:00
    int morning_end_min;
    int afternoon_start_hour;  // 15:00
    int afternoon_start_min;
    int afternoon_end_hour;    // 20:30
    int afternoon_end_min;
};
