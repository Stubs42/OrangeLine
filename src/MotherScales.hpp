/*
	MotherScales.hpp
 
Code for the OrangeLine module Mother

Copyright (C) 2019 Dieter Stubler

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#define SCALE_KEYS  44

    const char *scaleKeys[SCALE_KEYS]  = { 
        "2212221",      "2122212",      "1222122",      "2221221",      "2212212",      "2122122",      "1221222",
        "2221212",      "1212222",      "313131" ,      "22122111",     "321132",       "111111111111", "",
        "1322211",      "1312131",      "2131122",      "2121222",      "2212131",      "2122131",      "42141",
        "2131131",      "14214",        "14242",        "14142",        "2222121",      "2212121",      "2211222",
        "22323",        "2122221",      "32232",        "1222221",      "1222131",      "21212121",     "12121212",
        "1311231",      "1312122",      "222312",       "311223",       "132132",       "11411",        "2131212",
        "222222",       "32232"
    };
	const char *scaleNames[SCALE_KEYS] = {
        "Major",        "Dorian",       "Phrygian",     "Lydian",       "Myxolodian",   "Aeolian - natural Minor",   "Locrian",
        "Acoustic",     "Altered",      "Augmented",    "Bebop dom.",   "Blues",        "Chromatic",    "",
        "Enigmatic",    "Flamenco",     "Gypsy",        "Half diminished",    "harmonic Major",  "harmonic Minor",  "Hirajoshi",
        "Hungarian",    "Miyako-bushi", "Insen",        "Iwato",        "Lydian augmented",  "Bebob Major",   "Locrian Major",
        "Pentatonic Major",  "melodic Minor",   "Pentatonic Minor",  "Neapoliltan Major", "Neapolitan Minor", "Octatonic 1",  "Octatonic 2",
        "Persian",      "Phrygian dominant",  "Prometheus",   "Harmonics",    "Tritone",      "Tritone 2S",   "Ukrainian Dorian",
        "Wholetone",    "Yo"
    };
