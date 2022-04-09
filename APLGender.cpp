// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// http://www.viva64.com
// ReSharper disable CppClangTidyCertErr34C
// ReSharper disable CppClangTidyConcurrencyMtUnsafe
// ReSharper disable CppUseRangeAlgorithm
#include "../../../utils/file_iterator.hpp"
#include "../../../utils/my_timing.hpp"
#include "../../../utils/my_utils.hpp"
#include "../../../utils/gender.hpp"

#include <iostream>
#include <vector>
#include <array>
#include <utility> // swap
#include <ctime>

using namespace std;

std::string g_playlist_file;
std::string g_gender_file{"ArtistsGENDER.txt"};
bool g_verbose = false;

std::string now() {
    const time_t t = time(nullptr);
    struct tm buf = {};
    char str[26] = {};
    localtime_s(&buf, &t);
    asctime_s(str, sizeof str, &buf);
    const std::string s(str);
    return s.substr(0, s.size() - 1); // get rid of '\n'
}

std::string make_breaknote(
    const std::string_view note1 = "*** Gender conditioned ***",
    const std::string_view note2 = "") {

    std::string ret("N 00:00:00 ");
    ret += note1;
    ret += "|||";
    if (note2.empty()) {
        ret += now();
    } else {
        ret += note2;
    }
    ret += '\t';
    return ret;
}

void usage(const int argc, char** argv) {
    cout << "Usage: " << endl;
    cout << "PlaylistFile=C:\\Playlist Folder\\playlist-name.apl" << endl;
    cout << "[GenderFile=C:\\Playlist Folder\\ArtistsGENDER.txt]" << endl;

    if (argc >= 1) {
        cout << endl << endl << "Example: " << endl;
        cout << argv[0]
             << R"(PlaylistFile=C:\\server_root\\playlists\\030422.apl)" << endl
             << endl
             << endl;

        cout << "Or: " << endl;
        cout << argv[0]
             << "Playlist=C:\\server_root\\playlists\\030422.apl "
                "GenderFile=C:\\server_root\\my_gender_file.txt"
             << endl;

        cout << endl << endl;
        cout << "Notes: " << endl
             << "Where PlaylistFile is a valid DPS apl-style playlist," << endl;
        cout << "and GenderFile is a list of artists, tab-delimited with "
                "genders like the following: "
             << endl;
        cout << "Male: M, Female: F, Both: M/F" << endl;
        cout << "NB: that if no gender is found, FEMALE(F) is assumed." << endl;
        cout << "This program will attempt to separate male and female artists "
                "as much as possible by moving items in the playlist."
             << endl;
    }
}

int parse_args(const std::vector<std::string>& args) {

    if (args.empty()) {
        return -1;
    }
    constexpr auto const delim = '=';
    std::string all_args;

    for (const auto& s : args) {
        if (s.find(delim) == std::string::npos) {
            cerr << "No '=' delimiter found in string: " << endl;
            cerr << s << endl;
            return 1;
        }
        all_args.append(s);
    }

    const auto named_args = my::utils::strings::split<std::string>(
        std::string_view{all_args}, "=");

    // process pairs:
    if (named_args.size() % 2 != 0) {
        cerr << "Arguments not correct: should be PAIRS of name=value strings"
             << endl;
        return 1;
    }

    for (size_t i = 0; i < named_args.size(); i += 2) {
        if (const auto trimmed = my::utils::strings::trim_copy(named_args[i]);
            trimmed == "PlaylistFile" || trimmed == "Playlist") {
            g_playlist_file = my::utils::strings::trim_copy(named_args[i + 1]);
        } else if (trimmed == "GenderFile") {
            g_gender_file = my::utils::strings::trim_copy(named_args[i + 1]);
        } else {
            cerr << "Unrecognised option: " << trimmed << endl;
            return 0;
        }
    }

    if (!g_playlist_file.empty() && !g_gender_file.empty()) {
        return 0;
    }

    return -1;
}

using gender_map_t
    = my::utils::strings::case_insensitive_unordered_map<std::string, gender_t>;

auto make_genders(const std::string_view gender_file_path,
    std::vector<std::string_view>& splutlines) {

    gender_map_t ret{};
    std::string data;
    if (const auto ec = my::utils::file_open_and_read_all(
            std::string(gender_file_path), data);
        ec.code() != std::error_code()) {
        cerr << "Opening GenderFile " << gender_file_path << endl
             << "gave error: " << ec.what() << endl;
        exit(-101);
    }

    splutlines = my::utils::strings::split<std::string_view>(data, "\r\n");
    if (splutlines.empty()) {
        cerr << "Opening GenderFile " << gender_file_path << endl
             << "gave error: "
             << " no lines in genderfile";
        exit(-101);
    }

    int line_num = 0;
    for (const auto& this_line : splutlines) {
        ++line_num;
        auto splutTab = my::utils::strings::split<std::string>(this_line, "\t");
        if (splutTab.empty() || splutTab.size() != 2) {

            if (splutTab.empty()) {
                cerr << "Note: on line: " << line_num
                     << " in gender file: missing gender, assuming female"
                     << endl;
            } else {

                cerr << "Note: on line: " << line_num
                     << " in gender file, for artist: " << splutTab[0]
                     << " missing gender so "
                     << "assuming female " << endl;
            }

        } else {
            auto sgender = my::utils::strings::to_upper(splutTab[1]);
            my::utils::strings::trim(sgender);
            auto& art = splutTab[0];
            my::utils::strings::trim(art);
            if (art.empty()) {
                cerr << "Note: on line: " << line_num
                     << " in gender file: missing artist" << endl;
            } else {
                ret.insert({art, gender_from_string(sgender)});
            }
        }
    }

    return ret;
}

using hour_cuts_iterator = std::vector<artist_info>::iterator;

auto find_adjacent_males(std::vector<artist_info>& hour_cuts,
    const hour_cuts_iterator& where,
    gender_t consider_as_male = gender_t::male) {

    auto ret = std::adjacent_find(where, hour_cuts.end(),
        [consider_as_male](const auto& cut1, const auto& cut2) {
            return cut2.gender >= consider_as_male
                && cut1.gender >= consider_as_male
                && (cut1.dur > limits::jingle_ad_len
                    && cut1.dur < limits::max_song_len)
                && (cut2.dur > limits::jingle_ad_len
                    && cut2.dur < limits::max_song_len);
        });
    return ret;
}

auto find_adjacent_females(
    std::vector<artist_info>& hour_cuts, const hour_cuts_iterator& where) {
    return std::adjacent_find(
        where, hour_cuts.end(), [](const auto& cut1, const auto& cut2) {
            return (cut2.gender <= gender_t::female) // must be <= to account
                                                     // for assume_female
                && (cut1.gender <= gender_t::female)
                && (cut1.dur > limits::jingle_ad_len
                    && cut1.dur < limits::max_song_len)
                && (cut2.dur > limits::jingle_ad_len
                    && cut2.dur < limits::max_song_len);
        });
}

template <typename Collection, typename C = Collection>
void check_iters(const Collection& c, typename C::const_iterator iter1,
    typename C::const_iterator iter2) {
    (void)iter2;
    (void)iter1;
    (void)c;
    assert(iter1 >= c.begin() && iter1 < c.end());
    assert(iter2 >= c.begin() && iter2 < c.end());
}

// swapping hour items and enforcing sanity.
void checked_swap(std::vector<std::string_view>& file_lines, artist_info& a1,
    artist_info& a2, int hour_for) {

    (void)hour_for;
    // we should not be swapping any reserved cuts
    const auto d1 = a1.dur;
    const auto d2 = a2.dur;
    constexpr auto jingle_len = limits::jingle_ad_len;
    constexpr auto max_song_len = limits::max_song_len;

    assert(d1 > jingle_len);
    assert(d2 > jingle_len);
    assert(d2 < max_song_len);
    assert(d1 < max_song_len);
    swap(a1, a2);
    // swap these original file lines also:
    auto& a = file_lines[a1.line_number_orig];
    auto& b = file_lines[a2.line_number_orig];
    /*/ fixme: only when evo fixed!
    auto first_two = std::string(a.substr(0, 2));
    auto line_hour_for = std::atoi(first_two.c_str());
    assert(line_hour_for == hour_for);
    first_two = std::string(b.substr(0, 2));
    line_hour_for = std::atoi(first_two.c_str());
    assert(line_hour_for == hour_for);
    /*/

    if (&a != &b) {
        swap(a, b);
    }
    // don't swap the WRONG lines, ffs!
    assert(a.find(a1.artist) != std::string::npos);
    assert(b.find(a2.artist) != std::string::npos);
}

int check_attempts(const int n, const int max_attempts, const int nswaps,
    const int max_swaps, const int ihour) {
    if (n > max_attempts) {
        const std::string msg = my::utils::strings::concat(
            "In hour: ", ihour, " Too many attempts. Gave up!");
        return my::utils::handle_max(n, max_attempts, msg);
    }

    if (nswaps > max_swaps) {
        const std::string msg_too_many_attempts = my::utils::strings::concat(
            "In hour: ", ihour, " Too many swaps. Gave up!");
        return my::utils::handle_max(nswaps, max_swaps, msg_too_many_attempts);
    }
    return my::no_error;
}

using sv_vector_t = std::vector<std::string_view>;
using cuts_in_hour_t = std::vector<std::vector<artist_info>>;
using hour_iterator_t = std::vector<artist_info>::iterator;

using artists_in_hours_t = std::vector<std::vector<artist_info>>;
using intmap_t = std::map<int, int>;

void print_hour(const artists_in_hours_t::value_type& hour, const int hour_for,
    const std::string& extra = "", bool force = false,
    std::ostream& os = std::cout) {

    if (g_verbose || force) {
        os << "---------------------- Items in hour " << hour_for << extra
           << " ----------------------" << '\n';
        for (const auto& item : hour) {
            os << item << '\n';
        }
        os << "---------------------- Items in hour " << hour_for
           << " complete ----------------------\n\n";
    }
}
struct ctr_t {
    int hour_for = -1;
    int ctr = 0;
};

/*/
void show_running_totals(swaps_t running_swaps, attempts_t running_attempts) {
    if (running_swaps.hour_for < 0) {
        // cout << "No swaps carried out in this playlist " << endl;
    } else {
        if (g_verbose) {
            cout << "Most swaps in hour: " << running_swaps.hour_for << " ("
                 << running_swaps.swaps << ")." << endl;
            cout << endl;
        }
    }

    if (running_attempts.hour_for < 0) {
    } else {
        // if (g_verbose) {
        cout << "Most attempts in hour: " << running_attempts.hour_for << " ("
             << running_attempts.attempts << ")." << endl;
        cout << endl;
        //}
    }
}
/*/

int increment(ctr_t& t) {
    return t.ctr++;
}

struct hour_info_t {
    int hour_for = -1;
    int males = -1;
    int females = -1;
    gender_t accept_as_male = gender_t::male;
    bool success_expected = true;
};

void decide_accept_mf_as_male(
    vector<artist_info>& items, const int hour_for, hour_info_t& info) {

    auto n_males = static_cast<size_t>(std::count_if(
        items.begin(), items.end(), [](const auto& a) { return a.is_male(); }));
    const auto n_females = items.size() - n_males;
    const hour_info_t inf{hour_for, static_cast<int>(n_males),
        static_cast<int>(n_females), gender_t::male};
    info = inf;
    if (n_males < n_females) {
        n_males = std::count_if(items.begin(), items.end(),
            [](const auto& a) { return a.contains_male(); });
        if (n_males > inf.males) {
            info.males = static_cast<int>(n_males);
            info.accept_as_male = gender_t::male_female;
            if (info.males < info.females) {
                info.success_expected = false;
            }
        }
    }
}

auto is_fixable_hour(artists_in_hours_t::value_type& hour, const int ihour) {

    assert(ihour < 25);
    if (hour.size() < limits::not_enough_cuts_in_hour) {
        cout << "Hour: " << ihour << " has less than "
             << limits::not_enough_cuts_in_hour << " cuts in hour"
             << ", so is not eligible for gender separation." << endl
             << endl;
        return hour.end();
    }
    const auto found = find_adjacent_females(hour, hour.begin());
#ifndef NDEBUG
    artist_info prev;

    for (const auto& item : hour) {

        if (prev.is_female() && item.is_female() && prev.is_song()
            && item.is_song()) {
            if (!prev.is_empty()) {
                if (g_verbose) {
                    cout << "Found two non-males in " << ihour << " @\n"
                         << prev << "\n"
                         << item << endl;
                }
                assert(*found == prev);
                return found; // good enough
            }
        }
        prev = item;
    }
#endif
    return found;
}

void reorder_cuts_in_hours(sv_vector_t& file_lines,
    cuts_in_hour_t& artists_in_hours,
    const std::map<int, size_t>& hours_to_fix) {

    ctr_t ctr;

    for (auto hr_iter = hours_to_fix.begin(); hr_iter != hours_to_fix.end();
         ++hr_iter) {

        const int hour_for = hr_iter->first;
        auto& items = artists_in_hours[hour_for];
        hour_info_t hour_info;
        decide_accept_mf_as_male(items, hour_for, hour_info);

        int success = 0;

        // end here could be where ETM is found, if any
        auto fems = find_adjacent_females(items, items.begin());
        std::vector<artist_info>::iterator find_after = fems;
        while (fems != items.end()) {
            assert(fems->gender <= gender_t::female);
            auto males = find_adjacent_males(
                items, find_after, hour_info.accept_as_male);
            if (males != items.end()) {
                assert(males->gender >= hour_info.accept_as_male);
            }
            if (males == items.end()) {
                if (items.back().is_male()) {
                    checked_swap(file_lines, *(items.end() - 1), *fems,
                        hour_info.hour_for);
                    fems = find_adjacent_females(items, fems);
                    continue;
                } else {
                    if (auto men = find_adjacent_males(
                            items, items.begin(), hour_info.accept_as_male);
                        men == items.end()) {
                        // this really is the best we can do: no more
                        // consecutive males in the hour! so just move the final
                        // clash to the end of the hour and hope it's back timed
                        // away.
                        // ReSharper disable once CppTooWideScopeInitStatement
                        auto last_male = std::find_if(
                            items.rbegin(), items.rend(), [](const auto& info) {
                                return info.contains_male();
                            });
                        if (last_male != items.rend()) {
                            checked_swap(file_lines, *last_male, *fems,
                                hour_info.hour_for);
                        }
                        // should find B-52s
                        men = find_adjacent_males(
                            items, items.begin(), hour_info.accept_as_male);
                        success = -1;
                        break;
                    } else {
                        find_after = men;
                        continue;
                    }
                }
            }
            assert(males->gender >= hour_info.accept_as_male);
            assert(fems->gender <= gender_t::female);
            checked_swap(file_lines, *males, *fems++, hour_info.hour_for);
            fems = find_adjacent_females(items, fems);
            if (fems == items.end()) {
                fems = find_adjacent_females(items, items.begin());
            }
            ++find_after;
        }

        success = (fems == items.end());

        if (hour_info.success_expected) {
            if (is_fixable_hour(items, hour_for) != items.end()) {
                fems = find_adjacent_females(items, items.begin());
                assert(0);
            }
        } else {
            // it was an impossible task. I did what I could.
            // Just too many females.
        }

        assert(success != 0); /// did NOTHING
        if (success < 0) {
            assert("Unsuccessful" == nullptr);
            // failed to fix: too many females!
        } else {
        }

    } //  //for (auto iter = hours_to_fix...

    // show_running_totals(running_swaps, running_attempts);
}

using warned_t
    = my::utils::strings::case_insensitive_unordered_map<std::string_view,
        std::string_view>;

int pop_items(const gender_map_t& genders_by_artist, int& line_num,
    artists_in_hours_t& artists_in_hours,
    const std::vector<std::string_view>& file_lines, int ihour_for,
    warned_t& warned) {
    int prev_hour_for = 0;
    warned_t warned_defs;
    bool parsing_etm = false;

    for (const auto& line : file_lines) {

        int my_ihour_for = 0;
        if (line.find('N', 0) == 0 || parsing_etm) {
            // break notes do not count!
            // but we don't touch anything beyond an ETM
            if ((line.find("Exact Time Marker") != std::string::npos)
                || parsing_etm) {

                std::string stmp;
                if (line.find('N', 0) == 0) {
                    stmp = line.substr(2, 2);
                } else {
                    stmp = line.substr(0, 2);
                }
                if (const int my_int_hour_for
                    = std::atol(std::string(stmp).c_str());
                    my_int_hour_for > my_ihour_for) {
                    parsing_etm = false;
                } else {
                    // same etm hour
                    parsing_etm = true;
                    line_num++;
                    continue;
                }
            }
        } else {
            static constexpr const auto time_index = 0;
            static constexpr const auto duration_index = 4;
            static constexpr const auto artist_index = 1;
            auto splut_tab
                = my::utils::strings::split<std::string_view>(line, "\t");
            if (splut_tab.size() != 10) {
                cerr << "Unexpected, at playlist line: " << line_num << " in "
                     << g_playlist_file << endl
                     << "incorrect number of delimiters: expected " << 10
                     << " but got: " << splut_tab.size();
                return -7;
            }

            const auto& artist = splut_tab[artist_index];
            const auto& sdur = splut_tab[duration_index];
            const auto secs = my::utils::strings::HHMMSSto_secs(sdur);
            const auto& hour_for = splut_tab[time_index];
            my_ihour_for = std::atol(std::string(hour_for).c_str());
            assert(my_ihour_for < 25);

            // evo some playlists have 00::59:59 instead of the right hour
            if (my_ihour_for < ihour_for) {
                my_ihour_for = prev_hour_for;
            }
            if (my_ihour_for != ihour_for) {
                prev_hour_for = my_ihour_for;
                artists_in_hours.emplace_back((std::vector<artist_info>()));
                assert(artists_in_hours.size() <= 24);
                ihour_for = my_ihour_for;
                auto& arts_in_hour = artists_in_hours[my_ihour_for];
                arts_in_hour.reserve(
                    static_cast<std::vector<artist_info,
                        std::allocator<artist_info>>::size_type>(
                        limits::average_cuts_per_hour)
                    * 2);
            }

            auto& arts_in_hour = artists_in_hours[my_ihour_for];
            auto myfound = genders_by_artist.find(artist);
            auto gender{gender_t::assume_female};

            if (myfound != genders_by_artist.end()) {
                gender = myfound->second;
            } else {
                if (!warned.contains(artist)) {

                    if (g_verbose)
                        cerr << "**** NOTE: "
                             << "gender look-up for artist: "
                             << std::string(artist)
                             << " not available, assuming female." << endl;
                }
                warned.emplace(artist, artist);
                assert(!warned.empty());
            }

            // DO NOT put reserved items in here, otherwise
            // std::adjacent_find will not work!
            if (auto ai = artist_info(line_num, artist, secs, gender);
                ai.is_song()) {
                arts_in_hour.emplace_back(ai);
            } else {
                if (!warned_defs.contains(artist)) {
                    if (g_verbose)
                        cout << ai.to_short_string()
                             << " does not fit the definition of a 'song'."
                             << endl;
                    warned_defs.emplace(artist, artist);
                }
            }
        }
        line_num++;
    }
    if (g_verbose) cout << endl;

    return my::no_error;
}

int sanity(const int parse_result, const int argc, char** argv) {
    if (parse_result < 0) {
        usage(argc, argv);
    } else if (parse_result > 0) {
        return -1;
    }

    MYASSERT(!g_playlist_file.empty(), "playlist file is empty, bailing out.")

    if (!my::utils::file_exists(g_playlist_file)) {
        cerr << "PlaylistFile: '" << g_playlist_file << "' does not exist"
             << endl;
        return -2;
    }

    if (!my::utils::file_exists(g_gender_file)) {
        cerr << "GenderFile: '" << g_gender_file << "' does not exist" << endl;
        return -3;
    }
    return my::no_error;
}

void print_after_reordered(
    artists_in_hours_t& artists_in_hours, std::map<int, size_t>& hours_to_fix) {
    hours_to_fix.clear();

    int ihour_for = 0;
    int ctr = 0;
    for (auto& hour : artists_in_hours) {
        if (auto where = is_fixable_hour(hour, ihour_for);
            where != hour.end()) {
            if (ctr == 0) {
                cout << "#################### After reordering applied "
                     << "####################\n";
                ++ctr;
            }
            print_hour(hour, ihour_for, " after processing ", true);
            hours_to_fix[ihour_for] = ihour_for;
            cerr << "WARNING: hour " << ihour_for
                 << " still has consecutive females around:\n"
                 << *where << '\n'
                 << *(where + 1) << endl;
            assert(0); // what? you didn't do it 100%!
        } else {
            print_hour(hour, ihour_for, " after processing ");
        }

        ++ihour_for;
    }
    if (ctr > 0) {
        cout << "####################                       "
                "####################\n";
    }
}

auto find_fixable_hours(artists_in_hours_t artists_in_hours) {

    std::map<int, size_t> hours_to_fix;
    int ihour_for = 0;
    for (auto& hour : artists_in_hours) {
        print_hour(hour, ihour_for);

        if (auto fixable_where = is_fixable_hour(hour, ihour_for);
            fixable_where != hour.end()) {
            hours_to_fix[ihour_for] = fixable_where->line_number;
        }
        ++ihour_for;
    }
    return hours_to_fix;
}

bool process() {

    if (g_playlist_file.size() < 4) {
        cerr << "Cannot continue, playlist file path: " << g_playlist_file
             << " is empty or too short. Please provide one on the command "
                "line"
             << endl;
        Sleep(3000);
        exit(-7777);
    }

    if (!my::utils::file_exists(g_playlist_file)) {
        cerr << "Cannot continue, playlist file path: " << g_playlist_file
             << " does not exist. "
             << "\nNOTE: working directory is:\n"
             << my::utils::getcwd() << my::utils::getcwd() << endl;
        Sleep(3000);
        exit(-7777);
    }

    if (!my::utils::file_exists(g_gender_file)) {
        cerr << "Cannot continue, gender file path: " << g_gender_file
             << " does not exist. "
             << "\nNOTE: working directory is:\n"
             << my::utils::getcwd() << my::utils::getcwd() << endl;
        Sleep(3000);
        exit(-7777);
    }

    if (g_verbose)
        cout << "Processing file " << g_playlist_file << " ..." << endl;

    static std::vector<std::string_view> gender_lines{};
    static auto genders_by_artist = make_genders(g_gender_file, gender_lines);

    if (gender_lines.empty()) {
        cerr << "gender lines are empty " << endl;
        return false;
    }

    if (genders_by_artist.size() < 10) {
        cerr << "Not enough artists in genderfile: " << g_gender_file << endl;
        return false;
    }

    std::string tmp_file;
    if (const auto error
        = my::utils::file_copy(g_playlist_file, tmp_file, true);
        error != std::error_code()) {
        cerr << "Unable to make temp copy of " << g_playlist_file << endl;
        cerr << "Error was: " << error << endl;
        return false;
    }
    assert(!tmp_file.empty());

    std::string playlist_file_data;
    if (auto ec
        = my::utils::file_open_and_read_all(tmp_file, playlist_file_data);
        ec.code() != std::error_code()) {
        cerr << "Failed to read all contents of playlist file: " << tmp_file
             << ": " << ec.what() << endl;
        return false;
    }

    auto file_lines = my::utils::strings::split<std::string_view>(
        std::string_view{playlist_file_data}, "\r\n");

    if (file_lines.size() < 16) {
        cerr << "Not continuing, less than 16 lines in file: "
             << g_playlist_file << endl;
        return false;
    }

    artists_in_hours_t artists_in_hours;
    artists_in_hours.reserve(24);

    warned_t warned;
    int ihour_for = -1;
    int line_num = 0;
    pop_items(genders_by_artist, line_num, artists_in_hours, file_lines,
        ihour_for, warned);

    auto hours_to_fix = find_fixable_hours(artists_in_hours);

    if (g_verbose) {
        for (const auto& [fst, snd] : hours_to_fix) {
            cout << "Need to fix hour: " << fst << " at line: " << snd << endl;
        }
    }

    auto& m = hours_to_fix;
    reorder_cuts_in_hours(file_lines, artists_in_hours, m);
    print_after_reordered(artists_in_hours, m);

    if (g_verbose) cout << endl;
    std::string tmp_name = std::to_string(std::time(nullptr));
    tmp_name += ".tmp";

    std::string bn = make_breaknote();
    if (file_lines[0].find("*** Gender conditioned ***") == std::string::npos) {

        file_lines.insert(file_lines.begin(), bn);
    } else {
        file_lines[0] = bn;
    }
    if (auto err = my::utils::file_write_all(file_lines, tmp_name);
        err.code() != std::error_code()) {
        cerr << "FATAL: failed to write to temp file: " << tmp_file << endl;
        cerr << err.what() << endl;
        exit(-9999);
    } else {
        int r = ::remove(g_playlist_file.c_str());

        if (r != my::no_error) {
            cerr << "Failed to remove() [delete] original playlist, cannot "
                    "continue"
                 << endl;
            exit(-10000);
        }

        if (r = ::rename(tmp_name.c_str(), g_playlist_file.c_str()); r != 0) {
            cerr << "FATAL: failed to rename original playlist file " << endl
                 << "from " << tmp_file << endl
                 << "To: " << g_playlist_file << endl;
            exit(-10000);
        }
    }

    return true;
}

int mymain(int argc, char** argv) {

    if (g_verbose) my::stopwatch sw("Program execution took: ");

    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.size() == 1) { // allow dnd of playlist file onto exe
        const auto& a = args[0];
        g_playlist_file = a;

    } else {
        const auto parse_result = parse_args(args);
        cout << "Program starting ..." << endl;

        if (const int san_result = sanity(parse_result, argc, argv);
            san_result != my::no_error) {
            exit(san_result);
        }
    }

    if ([[maybe_unused]] const auto retval = process()) {
        return 0;
    } else {
        return -10000;
    }
}

int main(const int argc, char** argv) {

    try {
        auto dir = std::string(my::utils::getcwd());
        dir += my::utils::PATH_SEP_STR;
        my::listdir(dir.data(), [&](const auto& entry) {
            if (my::is_directory(entry)) {

            } else {
                if (std::string_view{entry->d_name}.ends_with(".apl")) {
                    g_playlist_file = std::string(dir);
                    g_playlist_file += std::string(entry->d_name);
                    process();
                }
            }
            return 0;
        });
    } catch (const std::exception& e) {
        cerr << "Failed, with exception: " << e.what() << endl;
        Sleep(4000);
    }

    return 0;

    try {
        const int ret = mymain(argc, argv);
        return ret;
    } catch (const std::exception& e) {
        cerr << e.what() << endl;
        Sleep(10000);
    }
}
