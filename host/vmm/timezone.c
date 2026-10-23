/*
 * maps host IANA time zones to Windows registry names under HKLM\SOFTWARE\Microsoft\Windows
 * NT\CurrentVersion\Time Zones. the table uses cities and regions named by each zone. unmatched zones leave
 * the guest with only the host UTC offset.
 */
#include "vmm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const struct {
    const char *Windows;
    const char *Iana; /* space separated. */
} g_zones[] = {
    {"Dateline Standard Time", "Etc/GMT+12"},
    {"UTC-11", "Etc/GMT+11 Pacific/Pago_Pago Pacific/Niue Pacific/Midway"},
    {"Aleutian Standard Time", "America/Adak America/Atka"},
    {"Hawaiian Standard Time", "Pacific/Honolulu Pacific/Johnston Pacific/Rarotonga Pacific/Tahiti Etc/GMT+10 US/Hawaii"},
    {"Marquesas Standard Time", "Pacific/Marquesas"},
    {"Alaskan Standard Time", "America/Anchorage America/Juneau America/Metlakatla America/Nome America/Sitka America/Yakutat US/Alaska"},
    {"UTC-09", "Etc/GMT+9 Pacific/Gambier"},
    {"Pacific Standard Time (Mexico)", "America/Tijuana America/Santa_Isabel"},
    {"UTC-08", "Etc/GMT+8 Pacific/Pitcairn"},
    {"Pacific Standard Time", "America/Los_Angeles America/Vancouver US/Pacific PST8PDT Canada/Pacific"},
    {"US Mountain Standard Time", "America/Phoenix America/Creston America/Dawson_Creek America/Fort_Nelson America/Hermosillo Etc/GMT+7 US/Arizona"},
    {"Mountain Standard Time (Mexico)", "America/Mazatlan"},
    {"Mountain Standard Time", "America/Denver America/Edmonton America/Cambridge_Bay America/Inuvik America/Yellowknife America/Ciudad_Juarez America/Boise MST7MDT US/Mountain Canada/Mountain"},
    {"Yukon Standard Time", "America/Whitehorse America/Dawson"},
    {"Central America Standard Time", "America/Guatemala America/Belize America/Costa_Rica America/El_Salvador America/Tegucigalpa America/Managua Pacific/Galapagos Etc/GMT+6"},
    {"Central Standard Time", "America/Chicago America/Winnipeg America/Indiana/Knox America/Indiana/Tell_City America/Matamoros America/Menominee America/North_Dakota/Beulah America/North_Dakota/Center America/North_Dakota/New_Salem America/Rainy_River America/Rankin_Inlet America/Resolute CST6CDT US/Central Canada/Central"},
    {"Central Standard Time (Mexico)", "America/Mexico_City America/Bahia_Banderas America/Merida America/Monterrey America/Chihuahua Mexico/General"},
    {"Canada Central Standard Time", "America/Regina America/Swift_Current Canada/Saskatchewan"},
    {"SA Pacific Standard Time", "America/Bogota America/Lima America/Guayaquil America/Rio_Branco America/Eirunepe America/Cayman America/Jamaica America/Panama America/Atikokan America/Coral_Harbour Etc/GMT+5"},
    {"Eastern Standard Time (Mexico)", "America/Cancun"},
    {"Eastern Standard Time", "America/New_York America/Nassau America/Toronto America/Detroit America/Iqaluit America/Kentucky/Louisville America/Kentucky/Monticello America/Nipigon America/Pangnirtung America/Thunder_Bay EST5EDT US/Eastern Canada/Eastern America/Montreal"},
    {"Haiti Standard Time", "America/Port-au-Prince"},
    {"Cuba Standard Time", "America/Havana Cuba"},
    {"US Eastern Standard Time", "America/Indiana/Indianapolis America/Indianapolis America/Indiana/Marengo America/Indiana/Petersburg America/Indiana/Vevay America/Indiana/Vincennes America/Indiana/Winamac US/East-Indiana"},
    {"Turks And Caicos Standard Time", "America/Grand_Turk"},
    {"Paraguay Standard Time", "America/Asuncion"},
    {"Atlantic Standard Time", "America/Halifax Atlantic/Bermuda America/Glace_Bay America/Goose_Bay America/Moncton America/Thule Canada/Atlantic"},
    {"Venezuela Standard Time", "America/Caracas"},
    {"Central Brazilian Standard Time", "America/Cuiaba America/Campo_Grande"},
    {"SA Western Standard Time", "America/La_Paz America/Antigua America/Anguilla America/Aruba America/Barbados America/Blanc-Sablon America/Boa_Vista America/Curacao America/Dominica America/Grenada America/Guadeloupe America/Guyana America/Kralendijk America/Lower_Princes America/Manaus America/Marigot America/Martinique America/Montserrat America/Port_of_Spain America/Porto_Velho America/Puerto_Rico America/Santo_Domingo America/St_Barthelemy America/St_Kitts America/St_Lucia America/St_Thomas America/St_Vincent America/Tortola Etc/GMT+4"},
    {"Pacific SA Standard Time", "America/Santiago Chile/Continental"},
    {"Newfoundland Standard Time", "America/St_Johns Canada/Newfoundland"},
    {"Tocantins Standard Time", "America/Araguaina"},
    {"E. South America Standard Time", "America/Sao_Paulo Brazil/East"},
    {"SA Eastern Standard Time", "America/Cayenne America/Fortaleza America/Belem America/Maceio America/Recife America/Santarem Antarctica/Rothera Atlantic/Stanley America/Paramaribo Etc/GMT+3"},
    {"Argentina Standard Time", "America/Argentina/Buenos_Aires America/Buenos_Aires America/Argentina/La_Rioja America/Argentina/Rio_Gallegos America/Argentina/Salta America/Argentina/San_Juan America/Argentina/San_Luis America/Argentina/Tucuman America/Argentina/Ushuaia America/Argentina/Catamarca America/Argentina/Cordoba America/Argentina/Jujuy America/Argentina/Mendoza America/Catamarca America/Cordoba America/Jujuy America/Mendoza"},
    {"Greenland Standard Time", "America/Godthab America/Nuuk"},
    {"Montevideo Standard Time", "America/Montevideo"},
    {"Magallanes Standard Time", "America/Punta_Arenas"},
    {"Saint Pierre Standard Time", "America/Miquelon"},
    {"Bahia Standard Time", "America/Bahia"},
    {"UTC-02", "Etc/GMT+2 America/Noronha Atlantic/South_Georgia Brazil/DeNoronha"},
    {"Azores Standard Time", "Atlantic/Azores America/Scoresbysund"},
    {"Cape Verde Standard Time", "Atlantic/Cape_Verde Etc/GMT+1"},
    {"UTC", "Etc/UTC Etc/GMT UTC GMT Etc/UCT Etc/Zulu Etc/Universal Etc/Greenwich UCT Zulu Universal Greenwich Etc/GMT+0 Etc/GMT-0 Etc/GMT0 GMT0 GMT+0 GMT-0"},
    {"GMT Standard Time", "Europe/London Atlantic/Canary Atlantic/Faroe Atlantic/Madeira Europe/Dublin Europe/Guernsey Europe/Isle_of_Man Europe/Jersey Europe/Lisbon Europe/Belfast GB Eire Portugal Atlantic/Faeroe"},
    {"Greenwich Standard Time", "Atlantic/Reykjavik Africa/Abidjan Africa/Accra Africa/Bamako Africa/Banjul Africa/Bissau Africa/Conakry Africa/Dakar Africa/Freetown Africa/Lome Africa/Monrovia Africa/Nouakchott Africa/Ouagadougou Atlantic/St_Helena Iceland Africa/Timbuktu"},
    {"Sao Tome Standard Time", "Africa/Sao_Tome"},
    {"Morocco Standard Time", "Africa/Casablanca Africa/El_Aaiun"},
    {"W. Europe Standard Time", "Europe/Berlin Europe/Andorra Europe/Vienna Europe/Zurich Europe/Busingen Europe/Gibraltar Europe/Rome Europe/Vaduz Europe/Luxembourg Europe/Monaco Europe/Malta Europe/Amsterdam Europe/Oslo Europe/Stockholm Arctic/Longyearbyen Europe/San_Marino Europe/Vatican Atlantic/Jan_Mayen"},
    {"Central Europe Standard Time", "Europe/Budapest Europe/Tirane Europe/Prague Europe/Podgorica Europe/Belgrade Europe/Ljubljana Europe/Bratislava"},
    {"Romance Standard Time", "Europe/Paris Europe/Brussels Europe/Copenhagen Europe/Madrid Africa/Ceuta"},
    {"Central European Standard Time", "Europe/Warsaw Europe/Sarajevo Europe/Zagreb Europe/Skopje Poland"},
    {"W. Central Africa Standard Time", "Africa/Lagos Africa/Algiers Africa/Luanda Africa/Porto-Novo Africa/Bangui Africa/Brazzaville Africa/Douala Africa/Kinshasa Africa/Libreville Africa/Malabo Africa/Ndjamena Africa/Niamey Africa/Tunis Etc/GMT-1"},
    {"Jordan Standard Time", "Asia/Amman"},
    {"GTB Standard Time", "Europe/Bucharest Asia/Nicosia Asia/Famagusta Europe/Athens Europe/Nicosia"},
    {"Middle East Standard Time", "Asia/Beirut"},
    {"Egypt Standard Time", "Africa/Cairo Egypt"},
    {"E. Europe Standard Time", "Europe/Chisinau Europe/Tiraspol"},
    {"Syria Standard Time", "Asia/Damascus"},
    {"West Bank Standard Time", "Asia/Hebron Asia/Gaza"},
    {"South Africa Standard Time", "Africa/Johannesburg Africa/Blantyre Africa/Bujumbura Africa/Gaborone Africa/Harare Africa/Kigali Africa/Lubumbashi Africa/Lusaka Africa/Maputo Africa/Maseru Africa/Mbabane Etc/GMT-2"},
    {"FLE Standard Time", "Europe/Kiev Europe/Kyiv Europe/Helsinki Europe/Mariehamn Europe/Riga Europe/Sofia Europe/Tallinn Europe/Uzhgorod Europe/Vilnius Europe/Zaporozhye"},
    {"Israel Standard Time", "Asia/Jerusalem Asia/Tel_Aviv Israel"},
    {"South Sudan Standard Time", "Africa/Juba"},
    {"Kaliningrad Standard Time", "Europe/Kaliningrad"},
    {"Sudan Standard Time", "Africa/Khartoum"},
    {"Libya Standard Time", "Africa/Tripoli Libya"},
    {"Namibia Standard Time", "Africa/Windhoek"},
    {"Arabic Standard Time", "Asia/Baghdad"},
    {"Turkey Standard Time", "Europe/Istanbul Asia/Istanbul Turkey"},
    {"Arab Standard Time", "Asia/Riyadh Asia/Bahrain Asia/Kuwait Asia/Qatar Asia/Aden"},
    {"Belarus Standard Time", "Europe/Minsk"},
    {"Russian Standard Time", "Europe/Moscow Europe/Kirov Europe/Simferopol W-SU"},
    {"E. Africa Standard Time", "Africa/Nairobi Africa/Djibouti Africa/Asmera Africa/Asmara Africa/Addis_Ababa Indian/Comoro Indian/Antananarivo Africa/Mogadishu Africa/Dar_es_Salaam Africa/Kampala Indian/Mayotte Antarctica/Syowa Etc/GMT-3"},
    {"Volgograd Standard Time", "Europe/Volgograd"},
    {"Iran Standard Time", "Asia/Tehran Iran"},
    {"Arabian Standard Time", "Asia/Dubai Asia/Muscat Etc/GMT-4"},
    {"Astrakhan Standard Time", "Europe/Astrakhan Europe/Ulyanovsk"},
    {"Azerbaijan Standard Time", "Asia/Baku"},
    {"Russia Time Zone 3", "Europe/Samara"},
    {"Mauritius Standard Time", "Indian/Mauritius Indian/Mahe Indian/Reunion"},
    {"Saratov Standard Time", "Europe/Saratov"},
    {"Georgian Standard Time", "Asia/Tbilisi"},
    {"Caucasus Standard Time", "Asia/Yerevan"},
    {"Afghanistan Standard Time", "Asia/Kabul"},
    {"West Asia Standard Time", "Asia/Tashkent Antarctica/Mawson Asia/Oral Asia/Aqtau Asia/Aqtobe Asia/Atyrau Indian/Maldives Indian/Kerguelen Asia/Dushanbe Asia/Ashgabat Asia/Ashkhabad Asia/Samarkand Etc/GMT-5"},
    {"Qyzylorda Standard Time", "Asia/Qyzylorda"},
    {"Ekaterinburg Standard Time", "Asia/Yekaterinburg"},
    {"Pakistan Standard Time", "Asia/Karachi"},
    {"India Standard Time", "Asia/Calcutta Asia/Kolkata"},
    {"Sri Lanka Standard Time", "Asia/Colombo"},
    {"Nepal Standard Time", "Asia/Katmandu Asia/Kathmandu"},
    {"Central Asia Standard Time", "Asia/Almaty Antarctica/Vostok Asia/Urumqi Indian/Chagos Asia/Bishkek Asia/Kashgar Etc/GMT-6"},
    {"Bangladesh Standard Time", "Asia/Dhaka Asia/Thimphu Asia/Dacca"},
    {"Omsk Standard Time", "Asia/Omsk"},
    {"Myanmar Standard Time", "Asia/Rangoon Asia/Yangon Indian/Cocos"},
    {"SE Asia Standard Time", "Asia/Bangkok Antarctica/Davis Indian/Christmas Asia/Jakarta Asia/Pontianak Asia/Phnom_Penh Asia/Vientiane Asia/Saigon Asia/Ho_Chi_Minh Etc/GMT-7"},
    {"Altai Standard Time", "Asia/Barnaul"},
    {"W. Mongolia Standard Time", "Asia/Hovd"},
    {"North Asia Standard Time", "Asia/Krasnoyarsk Asia/Novokuznetsk"},
    {"N. Central Asia Standard Time", "Asia/Novosibirsk"},
    {"Tomsk Standard Time", "Asia/Tomsk"},
    {"China Standard Time", "Asia/Shanghai Asia/Hong_Kong Asia/Macau Asia/Chongqing Asia/Harbin PRC Hongkong Asia/Macao"},
    {"North Asia East Standard Time", "Asia/Irkutsk"},
    {"Singapore Standard Time", "Asia/Singapore Antarctica/Casey Asia/Brunei Asia/Kuala_Lumpur Asia/Kuching Asia/Makassar Asia/Manila Etc/GMT-8 Singapore"},
    {"W. Australia Standard Time", "Australia/Perth Australia/West"},
    {"Taipei Standard Time", "Asia/Taipei ROC"},
    {"Ulaanbaatar Standard Time", "Asia/Ulaanbaatar Asia/Ulan_Bator Asia/Choibalsan"},
    {"Aus Central W. Standard Time", "Australia/Eucla"},
    {"Transbaikal Standard Time", "Asia/Chita"},
    {"Tokyo Standard Time", "Asia/Tokyo Asia/Jayapura Pacific/Palau Asia/Dili Etc/GMT-9 Japan"},
    {"North Korea Standard Time", "Asia/Pyongyang"},
    {"Korea Standard Time", "Asia/Seoul ROK"},
    {"Yakutsk Standard Time", "Asia/Yakutsk Asia/Khandyga"},
    {"Cen. Australia Standard Time", "Australia/Adelaide Australia/Broken_Hill Australia/South Australia/Yancowinna"},
    {"AUS Central Standard Time", "Australia/Darwin Australia/North"},
    {"E. Australia Standard Time", "Australia/Brisbane Australia/Lindeman Australia/Queensland"},
    {"AUS Eastern Standard Time", "Australia/Sydney Australia/Melbourne Australia/Canberra Australia/ACT Australia/NSW Australia/Victoria"},
    {"West Pacific Standard Time", "Pacific/Port_Moresby Antarctica/DumontDUrville Pacific/Chuuk Pacific/Truk Pacific/Guam Pacific/Saipan Etc/GMT-10"},
    {"Tasmania Standard Time", "Australia/Hobart Australia/Currie Australia/Tasmania Antarctica/Macquarie"},
    {"Vladivostok Standard Time", "Asia/Vladivostok Asia/Ust-Nera"},
    {"Lord Howe Standard Time", "Australia/Lord_Howe Australia/LHI"},
    {"Bougainville Standard Time", "Pacific/Bougainville"},
    {"Russia Time Zone 10", "Asia/Srednekolymsk"},
    {"Magadan Standard Time", "Asia/Magadan"},
    {"Norfolk Standard Time", "Pacific/Norfolk"},
    {"Sakhalin Standard Time", "Asia/Sakhalin"},
    {"Central Pacific Standard Time", "Pacific/Guadalcanal Pacific/Efate Pacific/Kosrae Pacific/Noumea Pacific/Pohnpei Pacific/Ponape Etc/GMT-11"},
    {"Russia Time Zone 11", "Asia/Kamchatka Asia/Anadyr"},
    {"New Zealand Standard Time", "Pacific/Auckland Antarctica/McMurdo Antarctica/South_Pole NZ"},
    {"UTC+12", "Etc/GMT-12 Pacific/Tarawa Pacific/Majuro Pacific/Kwajalein Pacific/Funafuti Pacific/Wake Pacific/Wallis Pacific/Nauru"},
    {"Fiji Standard Time", "Pacific/Fiji"},
    {"Chatham Islands Standard Time", "Pacific/Chatham NZ-CHAT"},
    {"UTC+13", "Etc/GMT-13 Pacific/Enderbury Pacific/Kanton Pacific/Fakaofo"},
    {"Tonga Standard Time", "Pacific/Tongatapu"},
    {"Samoa Standard Time", "Pacific/Apia"},
    {"Line Islands Standard Time", "Pacific/Kiritimati Etc/GMT-14"},
    {"Easter Island Standard Time", "Pacific/Easter Chile/EasterIsland"},
};

static bool listed(const char *list, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = list; *p;) {
        const char *end = strchr(p, ' ');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && !memcmp(p, name, n))
            return true;
        if (!end)
            break;
        p = end + 1;
    }
    return false;
}

const char *mime_windows_time_zone(const char *iana)
{
    if (!iana || !iana[0])
        return nullptr;
    for (size_t i = 0; i < sizeof(g_zones) / sizeof(g_zones[0]); i++)
        if (listed(g_zones[i].Iana, iana))
            return g_zones[i].Windows;
    return nullptr;
}

/* finds the host zone from /etc/localtime, TZ or /etc/timezone. */
bool mime_host_time_zone(char *out, size_t size)
{
    out[0] = 0;
    const char *tz = getenv("TZ");
    if (tz && tz[0]) {
        if (tz[0] == ':')
            tz++;
        const char *zoneinfo = strstr(tz, "zoneinfo/");
        mime_strlcpy(out, zoneinfo ? zoneinfo + 9 : tz, size);
        return out[0] != 0;
    }
    char link[512];
    ssize_t n = readlink("/etc/localtime", link, sizeof(link) - 1);
    if (n > 0) {
        link[n] = 0;
        const char *zoneinfo = strstr(link, "zoneinfo/");
        if (zoneinfo) {
            mime_strlcpy(out, zoneinfo + 9, size);
            return true;
        }
    }
    FILE *f = fopen("/etc/timezone", "r");
    if (f) {
        if (fgets(out, (int)size, f))
            out[strcspn(out, "\r\n")] = 0;
        fclose(f);
    }
    return out[0] != 0;
}
