// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include <core/path.h>
#include <core/str.h>

//------------------------------------------------------------------------------
TEST_CASE("path::get_base_name()")
{
    SECTION("Basic")
    {
        str<> s;

        SECTION("0") { path::get_base_name("one/two/three/filename.ext", s); }
        SECTION("1") { path::get_base_name("one/two/three/filename.ext", s); }
        SECTION("2") { path::get_base_name("one/two/three\\filename.ext", s); }
        SECTION("3") { path::get_base_name("filename.ext", s); }
        SECTION("4") { path::get_base_name("filename", s); }
        SECTION("5") { path::get_base_name("c:filename.ext", s); }
        SECTION("6") { path::get_base_name("c:filename", s); }
        SECTION("7") { path::get_base_name("\\\\?\\c:\\filename", s); }
        SECTION("8") { path::get_base_name("\\\\?\\c:filename", s); }

        REQUIRE(s.equals("filename"));
    }

    SECTION("Other")
    {
        str<> s;

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

//------------------------------------------------------------------------------
TEST_CASE("path::get_directory()")
{
    SECTION("Copy")
    {
        str<> s, t;

        SECTION("0") { t << "one/two/three/filename.ext"; }
        SECTION("1") { t << "one/two/three\\filename.ext"; }

        path::get_directory(t.c_str(), s);
        path::normalise(s, '/');
        REQUIRE(s.equals("one/two/three"));
    }

    SECTION("In-place")
    {
        str<> s;

        SECTION("0") { s << "one/two/three/filename.ext"; }
        SECTION("1") { s << "one/two\\three/filename.ext"; }

        path::get_directory(s);
        path::normalise(s, '/');
        REQUIRE(s.equals("one/two/three"));
    }

    SECTION("Trailing slash")
    {
        str<> s;

        SECTION("0") { s << "one/two/three/"; }
        SECTION("1") { s << "one/two/three\\"; }
        SECTION("2") { s << "one/two/three///"; }
        SECTION("3") { s << "one/two/three\\\\\\"; }

        path::get_directory(s);
        path::normalise(s, '/');
        REQUIRE(s.equals("one/two/three"));
    }

    SECTION("No slash")
    {
        str<> s;

        SECTION("0") { s << "one"; }
        SECTION("1") { s << ""; }

        path::get_directory(s);
        REQUIRE(s.equals(""));
    }

    SECTION("Root copy")
    {
        str<> s, t;

        SECTION("0") { t << "/"; }
        SECTION("1") { t << "\\"; }
        SECTION("2") { t << "/one"; }
        SECTION("3") { t << "\\one"; }

        path::get_directory(t.c_str(), s);
        path::normalise(s, '/');
        REQUIRE(s.equals("/"));
    }

    SECTION("Root in-place")
    {
        str<> s;

        SECTION("0") { s << "/"; }
        SECTION("1") { s << "\\"; }
        SECTION("2") { s << "/one"; }
        SECTION("3") { s << "\\one"; }

        path::get_directory(s);
        path::normalise(s, '/');
        REQUIRE(s.equals("/"));
    }

    SECTION("Drive letter")
    {
        str<> s;

        path::get_directory("e:\\one", s);
        REQUIRE(s.equals("e:\\"));

        s.clear();
        path::get_directory("e:/one", s);
        REQUIRE(s.equals("e:/"));

        s.clear();
        path::get_directory("e:one", s);
        REQUIRE(s.equals("e:"));
    }

    SECTION("UNC")
    {
        str<> s;

        path::get_directory("\\\\foo\\bar\\abc", s);
        REQUIRE(s.equals("\\\\foo\\bar"));

        s.clear();
        path::get_directory("\\\\foo\\bar\\", s);
        REQUIRE(s.equals("\\\\foo\\bar"));

        s.clear();
        path::get_directory("\\\\foo\\bar", s);
        REQUIRE(s.equals("\\\\foo\\bar"));

        s.clear();
        path::get_directory("\\\\foo", s);
        REQUIRE(s.equals("\\\\foo"));

        s.clear();
        path::get_directory("\\\\?\\UNC\\foo\\bar\\abc", s);
        REQUIRE(s.equals("\\\\?\\UNC\\foo\\bar"));

        s.clear();
        path::get_directory("\\\\?\\UNC\\foo\\bar\\", s);
        REQUIRE(s.equals("\\\\?\\UNC\\foo\\bar"));

        s.clear();
        path::get_directory("\\\\?\\UNC\\foo\\bar", s);
        REQUIRE(s.equals("\\\\?\\UNC\\foo\\bar"));

        s.clear();
        path::get_directory("\\\\?\\UNC\\foo", s);
        REQUIRE(s.equals("\\\\?\\UNC\\foo"));
    }
}

//------------------------------------------------------------------------------
TEST_CASE("path::get_drive()")
{
    SECTION("Has drive")
    {
        str<> s, t, u;

        SECTION("0") { s << "e:"; }
        SECTION("1") { s << "e:/"; }
        SECTION("2") { s << "e:/one/filename.ext"; }
        SECTION("3") { s << "e:one/filename.ext"; }
        SECTION("4") { s << "E:\\one/filename.ext"; }
        SECTION("5") { s << "E:one/filename.ext"; }
        SECTION("6") { s << "\\\\?\\E:\\filename.ext"; }
        SECTION("7") { s << "\\\\?\\E:filename.ext"; }

        u = s.c_str();

        REQUIRE(path::get_drive(s.c_str(), t));
        REQUIRE((t.equals("e:") || t.equals("E:")));
        REQUIRE(path::get_drive(t));

        REQUIRE(path::get_drive(u));
        REQUIRE(u.equals(t.c_str()));
    }

    SECTION("No drive")
    {
        str<> s, t;

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

//------------------------------------------------------------------------------
TEST_CASE("path::get_extension()")
{
    SECTION("Has extension")
    {
        str<> s;

        SECTION("0") { path::get_extension("one/two/three/filename.ext", s); }
        SECTION("1") { path::get_extension("one/two/three/filename.ext", s); }
        SECTION("2") { path::get_extension("one/two/three\\filename.ext", s); }
        SECTION("3") { path::get_extension("./two/three\\filename.ext", s); }
        SECTION("4") { path::get_extension("filename.ext", s); }
        SECTION("5") { path::get_extension("filename..ext", s); }
        SECTION("6") { path::get_extension(".ext", s); }

        REQUIRE(s.equals(".ext"));
    }

    SECTION("Misc")
    {
        str<> s;

        SECTION("0") { REQUIRE(!path::get_extension("..", s)); }
        SECTION("1") { REQUIRE(!path::get_extension("", s)); }
        SECTION("2") { REQUIRE(!path::get_extension("abc", s)); }
        SECTION("3") { REQUIRE(!path::get_extension("./one/two", s)); }
    }
}

//------------------------------------------------------------------------------
TEST_CASE("path::get_name()")
{
    SECTION("Basic")
    {
        str<> s;

        SECTION("0") { path::get_name("one/two/three/filename.ext", s); }
        SECTION("1") { path::get_name("one/two/three\\filename.ext", s); }
        SECTION("2") { path::get_name("filename.ext", s); }
        SECTION("3") { path::get_name("\\\\?\\filename.ext", s); }
        SECTION("4") { path::get_name("\\\\?\\c:filename.ext", s); }
        SECTION("5") { path::get_name("\\\\?\\UNC\\foo\\bar\\filename.ext", s); }

        REQUIRE(s.equals("filename.ext"));
    }

    SECTION("Trailing slash")
    {
        const char* name;

        SECTION("0") { name = path::get_name("one/two/"); }
        SECTION("1") { name = path::get_name("one/two//"); }
        SECTION("2") { name = path::get_name("one/two///"); }
        SECTION("3") { name = path::get_name("one\\two\\\\"); }
        SECTION("4") { name = path::get_name("/two/"); }
        SECTION("5") { name = path::get_name("two/"); }

        REQUIRE(name[0] == '\0');
    }

    SECTION("Other")
    {
        const char* in;

        SECTION("0") { in = ""; }
        SECTION("1") { in = "//"; }
        SECTION("2") { in = "/\\/"; }
        SECTION("3") { in = "\\"; }
        SECTION("4") { in = "\\\\?\\UNC\\foo"; }
        SECTION("4") { in = "\\\\?\\UNC\\foo\\bar"; }

        REQUIRE(path::get_name(in)[0] == '\0');
    }
}

//------------------------------------------------------------------------------
TEST_CASE("path::is_rooted()")
{
    SECTION("True")
    {
        REQUIRE(path::is_rooted("e:/"));
        REQUIRE(path::is_rooted("e:\\"));
        REQUIRE(path::is_rooted("/"));
        REQUIRE(path::is_rooted("\\"));
        REQUIRE(path::is_rooted("e:/one"));
        REQUIRE(path::is_rooted("e:\\one"));
        REQUIRE(path::is_rooted("/one"));
        REQUIRE(path::is_rooted("\\one"));
        REQUIRE(path::is_rooted("\\\\?\\e:\\one"));
        REQUIRE(path::is_rooted("\\\\?\\UNC\\foo\\bar\\one"));
        REQUIRE(path::is_rooted("\\\\foo\\bar\\one"));
    }

    SECTION("False")
    {
        REQUIRE(!path::is_rooted("e:"));
        REQUIRE(!path::is_rooted("e:one"));
        REQUIRE(!path::is_rooted("one"));
        REQUIRE(!path::is_rooted(""));
        REQUIRE(!path::is_rooted("\\\\?\\one"));
        REQUIRE(!path::is_rooted("\\\\?\\UNC\\foo"));
        REQUIRE(!path::is_rooted("\\\\foo\\bar"));
        REQUIRE(!path::is_rooted("\\\\foo"));
    }
}

//------------------------------------------------------------------------------
TEST_CASE("path::is_root()")
{
    SECTION("True")
    {
        REQUIRE(path::is_root("e:"));
        REQUIRE(path::is_root("e:/"));
        REQUIRE(path::is_root("e:\\"));
        REQUIRE(path::is_root("/"));
        REQUIRE(path::is_root("\\"));
        REQUIRE(path::is_root(""));
        REQUIRE(path::is_root("\\\\?\\e:"));
        REQUIRE(path::is_root("\\\\?\\e:/"));
        REQUIRE(path::is_root("\\\\?\\UNC\\foo\\bar"));
        REQUIRE(path::is_root("\\\\?\\UNC\\foo\\bar\\"));
        REQUIRE(path::is_root("\\\\?\\UNC\\one"));
    }

    SECTION("False")
    {
        REQUIRE(!path::is_root("e:one"));
        REQUIRE(!path::is_root("e:/one"));
        REQUIRE(!path::is_root("e:\\one"));
        REQUIRE(!path::is_root("/one"));
        REQUIRE(!path::is_root("\\one"));
        REQUIRE(!path::is_root("one"));
        REQUIRE(!path::is_root("\\\\?\\one"));
    }
}

//------------------------------------------------------------------------------
TEST_CASE("path::join()")
{
    SECTION("Basic")
    {
        str<> s;

        SECTION("0") { path::join("one/two", "three/four", s); }
        SECTION("1") { path::join("one/two/", "three/four", s); }
        SECTION("2") { path::join("one/two\\", "three/four", s); }

        path::normalise(s);
        REQUIRE(s.equals("one\\two\\three\\four"));
    }

    SECTION("Rooted rhs")
    {
        str<> s, t;

        SECTION("0") { t = "/three/four"; }
        SECTION("1") { t = "\\three/four"; }
        SECTION("2") { t = "a:\\three/four"; }
        SECTION("2") { t = "b:/three/four"; }

        path::append(s, t.c_str());
        path::normalise(s);
        path::normalise(t);
        REQUIRE(s.equals(t.c_str()));
    }

    SECTION("One side empty")
    {
        SECTION("Left")
        {
            str<> s;
            path::join("", "one\\two", s);
            REQUIRE(s.equals("one\\two"));
        }

        SECTION("Right")
        {
            str<> s;
            path::join("one\\two", "", s);
            REQUIRE(s.equals("one\\two\\"));
        }
    }

    SECTION("Drive letter")
    {
        str<> s;

        // Absolute
        s.copy("x:/");
        path::append(s, "one");
        REQUIRE(s.equals("x:/one"));
        s.copy("\\\\?\\x:/");
        path::append(s, "one");
        REQUIRE(s.equals("\\\\?\\x:/one"));

        s.copy("x:\\");
        path::append(s, "one");
        REQUIRE(s.equals("x:\\one"));
        s.copy("\\\\?\\x:\\");
        path::append(s, "one");
        REQUIRE(s.equals("\\\\?\\x:\\one"));

        // Relative
        s.copy("x:");
        path::append(s, "one");
        REQUIRE(s.equals("x:one"));
        s.copy("\\\\?\\x:");
        path::append(s, "one");
        REQUIRE(s.equals("\\\\?\\x:one"));
    }
}

//------------------------------------------------------------------------------
TEST_CASE("path::join(get_dir(), get_name())")
{
    const char* in;
    const char* out;

    SECTION("Plain")
    {
        SECTION("0") { in = "one/two"; }
        SECTION("1") { in = "one\\two"; }
        out = "one/two";
    }

    SECTION("Trailing slash")
    {
        SECTION("0") { in = "one/two/"; }
        SECTION("1") { in = "one/two\\"; }
        SECTION("2") { in = "one\\two\\"; }
        out = "one/two/";
    }

    str<> dir, name;
    path::get_directory(in, dir);
    path::get_name(in, name);

    str<> join;
    path::join(dir.c_str(), name.c_str(), join);
    path::normalise(join, '/');

    REQUIRE(join.equals(out));
}

//------------------------------------------------------------------------------
static void adjust_path(str_base& inout, int32 mode)
{
    if (mode)
    {
        str<> tmp(inout.c_str());
        const char* in = tmp.c_str();
        const bool unc = path::is_separator(in[0]) && path::is_separator(in[1]);
        inout.clear();
        if (unc)
            inout << "\\\\?\\UNC\\";
        else
            inout << "\\\\?\\";
        while (path::is_separator(*in))
            ++in;
        inout << in;
    }
}

//------------------------------------------------------------------------------
TEST_CASE("path::to_parent()")
{
    const char* name;
    const char* in;
    const char* out;
    const char* kid;
    const char* rejoined;

    for (int32 i = 0; i < 2; ++i)
    {
        SECTION("Normal")
        {
            name = SECTIONNAME();
            in = "c:/one/two";
            out = "c:/one";
            kid = "two";
            rejoined = "c:/one/two";
        }

        SECTION("Normal end")
        {
            name = SECTIONNAME();
            in = "c:/one/two/";
            out = "c:/one";
            kid = "two";
            rejoined = "c:/one/two";
        }

        SECTION("Normal root")
        {
            name = SECTIONNAME();
            in = "c:/one";
            out = "c:/";
            kid = "one";
            rejoined = "c:/one";
        }

        SECTION("Normal can't")
        {
            name = SECTIONNAME();
            in = "c:/";
            out = "c:/";
            kid = "";
            rejoined = "c:/";
        }

        SECTION("Drive")
        {
            name = SECTIONNAME();
            in = "c:";
            out = "c:";
            kid = "";
            rejoined = "c:";
        }

        SECTION("UNC")
        {
            name = SECTIONNAME();
            in = "//foo/bar/abc/def";
            out = "//foo/bar/abc";
            kid = "def";
            rejoined = "//foo/bar/abc/def";
        }

        SECTION("UNC end")
        {
            name = SECTIONNAME();
            in = "//foo/bar/abc/def/";
            out = "//foo/bar/abc";
            kid = "def";
            rejoined = "//foo/bar/abc/def";
        }

        SECTION("UNC root")
        {
            name = SECTIONNAME();
            in = "//foo/bar/abc";
            out = "//foo/bar";
            kid = "abc";
            rejoined = "//foo/bar/abc";
        }

        SECTION("UNC can't")
        {
            name = SECTIONNAME();
            in = "//foo/bar";
            out = "//foo/bar";
            kid = "";
            rejoined = "//foo/bar/";
        }

        SECTION("UNC can't end")
        {
            name = SECTIONNAME();
            in = "//foo/bar/";
            out = "//foo/bar";
            kid = "";
            rejoined = "//foo/bar/";
        }

        SECTION("No seps")
        {
            name = SECTIONNAME();
            in = "foo";
            out = "";
            kid = "foo";
            rejoined = "foo";
        }
        str<> parent, child;
        str<> orig, verify;

        parent = in;
        orig = in;
        verify = out;

        adjust_path(parent, i);
        adjust_path(orig, i);
        adjust_path(verify, i);

        path::to_parent(parent, &child);
        REQUIRE(parent.equals(verify.c_str()), [&] () { printf("%s%s\n\nplain input:\t\t'%s'\nexpected parent:\t'%s'\nactual parent:\t\t'%s'", name, i ? " (namespace)" : "", orig.c_str(), verify.c_str(), parent.c_str()); });
        REQUIRE(child.equals(kid), [&] () { printf("%s%s\n\ninput:\t\t\t'%s'\nexpected child:\t'%s'\nactual child:\t\t'%s'", name, i ? " (namespace)" : "", orig.c_str(), kid, child.c_str()); });
        path::append(parent, child.c_str());

        parent = in;
        adjust_path(parent, i);
        path::normalise(parent, '\\');
        path::normalise(orig, '\\');
        path::normalise(verify, '\\');

        path::to_parent(parent, &child);
        REQUIRE(parent.equals(verify.c_str()),  [&] () { printf("%s%s\n\nnormalized input:\t'%s'\nexpected parent:\t'%s'\nactual parent:\t\t'%s'", name, i ? " (namespace)" : "", orig.c_str(), verify.c_str(), parent.c_str()); });
        REQUIRE(child.equals(kid), [&] () { printf("%s%s\n\nnormalized input:\t'%s'\nexpected child:\t'%s'\nactual child:\t\t'%s'", name, i ? " (namespace)" : "", orig.c_str(), kid, child.c_str()); });

        verify = rejoined;
        adjust_path(verify, i);
        path::normalise(verify, '\\');
        path::append(parent, child.c_str());
        REQUIRE(parent.equals(verify.c_str()), [&] () { printf("%s%s\n\nexpected rejoined:\t'%s'\nactual rejoined:\t'%s'", name, i ? " (namespace)" : "", verify.c_str(), parent.c_str()); });
    }
}

//------------------------------------------------------------------------------
TEST_CASE("path::normalise() 1")
{
    str<> s;

    s.copy("X://0/\\/1/2//\\3/\\\\//4//");
    path::normalise(s);
    REQUIRE(s.equals("X:\\0\\1\\2\\3\\4\\"));

    s << "//\\//";
    path::normalise(s, '/');
    REQUIRE(s.equals("X:/0/1/2/3/4/"));

    s.copy("abcdef");
    path::normalise(s);
    REQUIRE(s.equals("abcdef"));
}

//------------------------------------------------------------------------------
TEST_CASE("path::normalise() 2")
{
    auto test = [] (const char* test_path, const char* expected)
    {
        str<> out(test_path);
        path::normalise(out, '/');
        REQUIRE(out.equals(expected), [&] () {
            puts(test_path);
            puts(expected);
            puts(out.c_str());
        });
    };

    test("123/xxx/xxx/../../456", "123/456");
    test("/123/xxx/xxx/../../456", "/123/456");
    test("a:/123/xxx/xxx/../../456", "a:/123/456");
    test("a:/123/xxx/xxx/../../456/", "a:/123/456/");
    test("a:/123/xxx/xxx/../../../../../../456/", "a:/456/");
    test("a:/xxx/yyy/../../123/456", "a:/123/456");
    test("a:/xxx/../../123/456", "a:/123/456");
    test("a:/xxx/../../123/456", "a:/123/456");
    test("a:/123/xxx/..", "a:/123/");
    test("a:/123/./xxx/..", "a:/123/");
    test("a:/123/.", "a:/123/");

    test("a:/1/2/x/../../2/3/4", "a:/1/2/3/4");
    test("/1/x/../../2/3/4", "/2/3/4");
    test("a://1/2//../2", "a:/1/2");
    test("a:/1//2////../2", "a:/1/2");
    test("a://1/../1/2", "a:/1/2");

    test("a:/1/./2/", "a:/1/2/");

    test("", "");
    test("/..", "/");

    test("..", "..");
    test("../..", "../..");
    test("../../..", "../../..");
    test("../xxx/../..", "../..");
    test("../../xxx/../..", "../../..");

    test("//?/foo/../..", "//?/");
    test("//?/a:/..", "//?/a:/");
    test("//?/UNC/foo/bar/..", "//?/UNC/foo/bar");
    test("//?/UNC/foo/bar/../abc", "//?/UNC/foo/bar/abc");
}
