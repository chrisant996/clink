/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "catch.hpp"

#include <core/path.h>
#include <core/str.h>

TEST_CASE("Paths") {
    str<> s, t;

    SECTION("path::clean()") {
        s.copy("X://0/\\/1/2//\\3/\\\\//4//");
        path::clean(s);
        REQUIRE(s.equals("X:\\0\\1\\2\\3\\4\\"));

        s.copy("abcdef");
        path::clean(s);
        REQUIRE(s.equals("abcdef"));
    }

    SECTION("path::get_base_name()") {
        SECTION("Basic") {
            SECTION("0") { path::get_base_name("one/two/three/filename.ext", s); }
            SECTION("1") { path::get_base_name("one/two/three/filename.ext", s); }
            SECTION("2") { path::get_base_name("one/two/three\\filename.ext", s); }
            SECTION("3") { path::get_base_name("filename.ext", s); }
            SECTION("4") { path::get_base_name("filename", s); }

            REQUIRE(s.equals("filename"));
        }

        SECTION("Other") {
            path::get_base_name("one/two/three/filename...ext", s);
            REQUIRE(s.equals("filename.."));
            s.clear();

            path::get_base_name("one/two/three/filename..ext.ext", s);
            REQUIRE(s.equals("filename..ext"));
            s.clear();

            path::get_base_name("one/two/three/filename...", s);
            REQUIRE(s.equals("filename.."));
            s.clear();

            path::get_base_name("one/two/three/filename.ext.", s);
            REQUIRE(s.equals("filename.ext"));
            s.clear();
        }

    }

    SECTION("path::get_directory()") {
        SECTION("Basic") {
            SECTION("0") { t << "one/two/three/filename.ext"; }
            SECTION("1") { t << "one/two/three\\filename.ext"; }
            path::get_directory(t.c_str(), s);
        }

        SECTION("In-place") {
            SECTION("0") { s << "one/two/three/filename.ext"; }
            SECTION("1") { s << "one/two/three\\filename.ext"; }
            path::get_directory(s);
        }

        REQUIRE(s.equals("one/two/three"));
    }

    SECTION("path::get_drive()") {
        SECTION("Has drive") {
            SECTION("0") { s << "e:"; }
            SECTION("1") { s << "e:/"; }
            SECTION("2") { s << "e:/one/filename.ext"; }
            SECTION("3") { s << "e:one/filename.ext"; }
            SECTION("4") { s << "E:\\one/filename.ext"; }
            SECTION("5") { s << "E:one/filename.ext"; }

            REQUIRE(path::get_drive(s.c_str(), t));
            REQUIRE((t.equals("e:") || t.equals("E:")));
            REQUIRE(path::get_drive(t));
        }

        SECTION("No drive") {
            SECTION("0") { s << ""; }
            SECTION("1") { s << ":"; }
            SECTION("2") { s << ":/"; }
            SECTION("3") { s << "[:/"; }
            SECTION("4") { s << "{:"; }
            SECTION("5") { s << "@:"; }
            SECTION("6") { s << "`:\\"; }
            SECTION("7") { s << "/one/filename.ext"; }
            SECTION("8") { s << "one/filename.ext"; }
            SECTION("9") { s << "filename.ext"; }

            REQUIRE(!path::get_drive(s.c_str(), t));
            REQUIRE(!path::get_drive(s));
        }
    }

    SECTION("path::get_extension()") {
        SECTION("Has extension") {
            SECTION("0") { path::get_extension("one/two/three/filename.ext", s); }
            SECTION("1") { path::get_extension("one/two/three/filename.ext", s); }
            SECTION("2") { path::get_extension("one/two/three\\filename.ext", s); }
            SECTION("3") { path::get_extension("filename.ext", s); }
            SECTION("3") { path::get_extension("filename..ext", s); }
            SECTION("4") { path::get_extension(".ext", s); }

            REQUIRE(s.equals(".ext"));
        }

        SECTION("Misc") {
            SECTION("0") {
                path::get_extension("..", s);
                REQUIRE(s.equals("."));
            }
            SECTION("1") { REQUIRE(!path::get_extension("", s)); }
            SECTION("2") { REQUIRE(!path::get_extension("abc", s)); }
        }
    }

    SECTION("path::get_name()") {
        SECTION("Basic") {
            SECTION("0") { path::get_name("one/two/three/filename.ext", s); }
            SECTION("1") { path::get_name("one/two/three\\filename.ext", s); }
            SECTION("2") { path::get_name("filename.ext", s); }

            REQUIRE(s.equals("filename.ext"));
        }
    }

    SECTION("path::join()") {
        SECTION("Basic") {
            SECTION("0") { path::join("one/two", "three/four", s); }
            SECTION("1") { path::join("one/two/", "three/four", s); }
            SECTION("2") { path::join("one/two\\", "three/four", s); }
            SECTION("3") { path::join("one/two", "/three/four", s); }
            SECTION("4") { path::join("one/two", "\\three/four", s); }

            path::clean(s);
            REQUIRE(s.equals("one\\two\\three\\four"));
        }
    }
}
