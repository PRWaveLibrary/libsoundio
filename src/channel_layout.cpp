/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "soundio_private.h"

#include <stdio.h>

static struct SoundIoChannelLayout builtin_channel_layouts[] = {
    {
        "Mono",
        1,
        {
            SoundIoChannelIdFrontCenter,
        },
    },
    {
        "Stereo",
        2,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
        },
    },
    {
        "2.1",
        3,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdLfe,
        },
    },
    {
        "3.0",
        3,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
        }
    },
    {
        "3.0 (back)",
        3,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdBackCenter,
        }
    },
    {
        "3.1",
        4,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdLfe,
        }
    },
    {
        "4.0",
        4,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdBackCenter,
        }
    },
    {
        "Quad",
        4,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdBackLeft,
            SoundIoChannelIdBackRight,
        },
    },
    {
        "Quad (side)",
        4,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdSideLeft,
            SoundIoChannelIdSideRight,
        }
    },
    {
        "4.1",
        5,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdBackCenter,
            SoundIoChannelIdLfe,
        }
    },
    {
        "5.0 (back)",
        5,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdBackLeft,
            SoundIoChannelIdBackRight,
        }
    },
    {
        "5.0 (side)",
        5,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdSideLeft,
            SoundIoChannelIdSideRight,
        }
    },
    {
        "5.1",
        6,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdSideLeft,
            SoundIoChannelIdSideRight,
            SoundIoChannelIdLfe,
        }
    },
    {
        "5.1 (back)",
        6,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdBackLeft,
            SoundIoChannelIdBackRight,
            SoundIoChannelIdLfe,
        }
    },
    {
        "6.0 (side)",
        6,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdSideLeft,
            SoundIoChannelIdSideRight,
            SoundIoChannelIdBackCenter,
        }
    },
    {
        "6.0 (front)",
        6,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdSideLeft,
            SoundIoChannelIdSideRight,
            SoundIoChannelIdFrontLeftCenter,
            SoundIoChannelIdFrontRightCenter,
        }
    },
    {
        "Hexagonal",
        6,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdBackLeft,
            SoundIoChannelIdBackRight,
            SoundIoChannelIdBackCenter,
        }
    },
    {
        "6.1",
        7,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdSideLeft,
            SoundIoChannelIdSideRight,
            SoundIoChannelIdBackCenter,
            SoundIoChannelIdLfe,
        }
    },
    {
        "6.1 (back)",
        7,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdBackLeft,
            SoundIoChannelIdBackRight,
            SoundIoChannelIdBackCenter,
            SoundIoChannelIdLfe,
        }
    },
    {
        "6.1 (front)",
        7,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdSideLeft,
            SoundIoChannelIdSideRight,
            SoundIoChannelIdFrontLeftCenter,
            SoundIoChannelIdFrontRightCenter,
            SoundIoChannelIdLfe,
        }
    },
    {
        "7.0",
        7,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdSideLeft,
            SoundIoChannelIdSideRight,
            SoundIoChannelIdBackLeft,
            SoundIoChannelIdBackRight,
        }
    },
    {
        "7.0 (front)",
        7,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdSideLeft,
            SoundIoChannelIdSideRight,
            SoundIoChannelIdFrontLeftCenter,
            SoundIoChannelIdFrontRightCenter,
        }
    },
    {
        "7.1",
        8,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdSideLeft,
            SoundIoChannelIdSideRight,
            SoundIoChannelIdBackLeft,
            SoundIoChannelIdBackRight,
            SoundIoChannelIdLfe,
        }
    },
    {
        "7.1 (wide)",
        8,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdSideLeft,
            SoundIoChannelIdSideRight,
            SoundIoChannelIdFrontLeftCenter,
            SoundIoChannelIdFrontRightCenter,
            SoundIoChannelIdLfe,
        }
    },
    {
        "7.1 (wide) (back)",
        8,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdBackLeft,
            SoundIoChannelIdBackRight,
            SoundIoChannelIdFrontLeftCenter,
            SoundIoChannelIdFrontRightCenter,
            SoundIoChannelIdLfe,
        }
    },
    {
        "Octagonal",
        8,
        {
            SoundIoChannelIdFrontLeft,
            SoundIoChannelIdFrontRight,
            SoundIoChannelIdFrontCenter,
            SoundIoChannelIdSideLeft,
            SoundIoChannelIdSideRight,
            SoundIoChannelIdBackLeft,
            SoundIoChannelIdBackRight,
            SoundIoChannelIdBackCenter,
        }
    },
};


#define CHANNEL_NAME_ALIAS_COUNT 3
static const wchar_t* channel_names[][CHANNEL_NAME_ALIAS_COUNT] = {
    {L"(Invalid Channel)", nullptr, nullptr},
    {L"Front Left", L"FL", L"front-left"},
    {L"Front Right", L"FR", L"front-right"},
    {L"Front Center", L"FC", L"front-center"},
    {L"LFE", L"LFE", L"lfe"},
    {L"Back Left", L"BL", L"rear-left"},
    {L"Back Right", L"BR", L"rear-right"},
    {L"Front Left Center", L"FLC", L"front-left-of-center"},
    {L"Front Right Center", L"FRC", L"front-right-of-center"},
    {L"Back Center", L"BC", L"rear-center"},
    {L"Side Left", L"SL", L"side-left"},
    {L"Side Right", L"SR", L"side-right"},
    {L"Top Center", L"TC", L"top-center"},
    {L"Top Front Left", L"TFL", L"top-front-left"},
    {L"Top Front Center", L"TFC", L"top-front-center"},
    {L"Top Front Right", L"TFR", L"top-front-right"},
    {L"Top Back Left", L"TBL", L"top-rear-left"},
    {L"Top Back Center", L"TBC", L"top-rear-center"},
    {L"Top Back Right", L"TBR", L"top-rear-right"},
    {L"Back Left Center", nullptr, nullptr},
    {L"Back Right Center", nullptr, nullptr},
    {L"Front Left Wide", nullptr, nullptr},
    {L"Front Right Wide", nullptr, nullptr},
    {L"Front Left High", nullptr, nullptr},
    {L"Front Center High", nullptr, nullptr},
    {L"Front Right High", nullptr, nullptr},
    {L"Top Front Left Center", nullptr, nullptr},
    {L"Top Front Right Center", nullptr, nullptr},
    {L"Top Side Left", nullptr, nullptr},
    {L"Top Side Right", nullptr, nullptr},
    {L"Left LFE", nullptr, nullptr},
    {L"Right LFE", nullptr, nullptr},
    {L"LFE 2", nullptr, nullptr},
    {L"Bottom Center", nullptr, nullptr},
    {L"Bottom Left Center", nullptr, nullptr},
    {L"Bottom Right Center", nullptr, nullptr},
    {L"Mid/Side Mid", nullptr, nullptr},
    {L"Mid/Side Side", nullptr, nullptr},
    {L"Ambisonic W", nullptr, nullptr},
    {L"Ambisonic X", nullptr, nullptr},
    {L"Ambisonic Y", nullptr, nullptr},
    {L"Ambisonic Z", nullptr, nullptr},
    {L"X-Y X", nullptr, nullptr},
    {L"X-Y Y", nullptr, nullptr},
    {L"Headphones Left", nullptr, nullptr},
    {L"Headphones Right", nullptr, nullptr},
    {L"Click Track", nullptr, nullptr},
    {L"Foreign Language", nullptr, nullptr},
    {L"Hearing Impaired", nullptr, nullptr},
    {L"Narration", nullptr, nullptr},
    {L"Haptic", nullptr, nullptr},
    {L"Dialog Centric Mix", nullptr, nullptr},
    {L"Aux", nullptr, nullptr},
    {L"Aux 0", nullptr, nullptr},
    {L"Aux 1", nullptr, nullptr},
    {L"Aux 2", nullptr, nullptr},
    {L"Aux 3", nullptr, nullptr},
    {L"Aux 4", nullptr, nullptr},
    {L"Aux 5", nullptr, nullptr},
    {L"Aux 6", nullptr, nullptr},
    {L"Aux 7", nullptr, nullptr},
    {L"Aux 8", nullptr, nullptr},
    {L"Aux 9", nullptr, nullptr},
    {L"Aux 10", nullptr, nullptr},
    {L"Aux 11", nullptr, nullptr},
    {L"Aux 12", nullptr, nullptr},
    {L"Aux 13", nullptr, nullptr},
    {L"Aux 14", nullptr, nullptr},
    {L"Aux 15", nullptr, nullptr},
};

const std::wstring soundio_get_channel_name(enum SoundIoChannelId id)
{
    if (id >= std::size(channel_names))
    {
        return L"(Invalid Channel)";
    }
    auto p = channel_names[id][0];
    if (p == nullptr)
    {
        return std::wstring();
    }

    return p;
}

bool soundio_channel_layout_equal(const struct SoundIoChannelLayout* a, const struct SoundIoChannelLayout* b)
{
    if (a->channel_count != b->channel_count)
        return false;

    for (int i = 0; i < a->channel_count; i += 1)
    {
        if (a->channels[i] != b->channels[i])
            return false;
    }

    return true;
}

int soundio_channel_layout_builtin_count()
{
    return std::size(builtin_channel_layouts);
}

const struct SoundIoChannelLayout* soundio_channel_layout_get_builtin(int index)
{
    assert(index >= 0);
    assert(index <= std::size(builtin_channel_layouts));
    return &builtin_channel_layouts[index];
}

int soundio_channel_layout_find_channel(
    const struct SoundIoChannelLayout* layout, enum SoundIoChannelId channel)
{
    for (int i = 0; i < layout->channel_count; i += 1)
    {
        if (layout->channels[i] == channel)
            return i;
    }
    return -1;
}

bool soundio_channel_layout_detect_builtin(struct SoundIoChannelLayout* layout)
{
    for (int i = 0; i < std::size(builtin_channel_layouts); i += 1)
    {
        const struct SoundIoChannelLayout* builtin_layout = &builtin_channel_layouts[i];
        if (soundio_channel_layout_equal(builtin_layout, layout))
        {
            layout->name = builtin_layout->name;
            return true;
        }
    }
    layout->name = nullptr;
    return false;
}

const struct SoundIoChannelLayout* soundio_channel_layout_get_default(int channel_count)
{
    switch (channel_count)
    {
        case 1:
            return soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdMono);
        case 2:
            return soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdStereo);
        case 3:
            return soundio_channel_layout_get_builtin(SoundIoChannelLayoutId3Point0);
        case 4:
            return soundio_channel_layout_get_builtin(SoundIoChannelLayoutId4Point0);
        case 5:
            return soundio_channel_layout_get_builtin(SoundIoChannelLayoutId5Point0Back);
        case 6:
            return soundio_channel_layout_get_builtin(SoundIoChannelLayoutId5Point1Back);
        case 7:
            return soundio_channel_layout_get_builtin(SoundIoChannelLayoutId6Point1);
        case 8:
            return soundio_channel_layout_get_builtin(SoundIoChannelLayoutId7Point1);
    }
    return NULL;
}

enum SoundIoChannelId soundio_parse_channel_id(const std::wstring str)
{
    for (int id = 0; id < std::size(channel_names); id += 1)
    {
        for (int i = 0; i < CHANNEL_NAME_ALIAS_COUNT; i += 1)
        {
            const wchar_t* p = channel_names[id][i];
            if (p == nullptr)
            {
                break;
            }

            const std::wstring alias = p;
            if (alias == str)
            {
                return static_cast<SoundIoChannelId>(id);
            }
        }
    }
    return SoundIoChannelIdInvalid;
}
