/*
* If not stated otherwise in this file or this component's LICENSE file the
* following copyright and licenses apply:
*
* Copyright 2023 Sky UK
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#pragma once

#include <inja/inja.hpp>
#include <cstdio>
#include <string>

inline std::string htmlEscape(const nlohmann::json &value)
{
    const std::string text = value.is_string() ? value.get<std::string>() : value.dump();
    std::string escaped;
    escaped.reserve(text.size());

    for (const char character : text) {
        switch (character) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&#39;"; break;
        default: escaped += character; break;
        }
    }

    return escaped;
}

inline void registerHtmlEscapeCallback(inja::Environment &env)
{
    env.add_callback("htmlEscape", 1, [](inja::Arguments &args)
    {
        return htmlEscape(*args.at(0));
    });
}

inline void registerJsEscapeCallback(inja::Environment &env)
{
    env.add_callback("jsEscape", 1, [](inja::Arguments &args)
    {
        const std::string input = args.at(0)->get<std::string>();
        std::string escaped;
        escaped.reserve(input.size() + input.size() / 4);

        for (char c : input) {
            switch (c) {
            case '\\': escaped += "\\\\"; break;
            case '\'': escaped += "\\'"; break;
            case '"':  escaped += "\\\""; break;
            case '<':  escaped += "\\x3c"; break;
            case '>':  escaped += "\\x3e"; break;
            case '/':  escaped += "\\/"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\0': escaped += "\\0"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\x%02x", static_cast<unsigned char>(c));
                    escaped += buf;
                } else {
                    escaped += c;
                }
                break;
            }
        }

        return escaped;
    });
}

