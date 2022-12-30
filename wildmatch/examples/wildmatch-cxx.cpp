#include <cstdlib>
#include <cstring>
#include <iostream>

#include <wildmatch/wildmatch.hpp>

void die(const char *msg)
{
    std::cerr << "error: " << msg << std::endl;
    exit(1);
}

int main(int argc, char **argv)
{
    int i;
    if (argc < 4) {
        die("usage: wildmatch <mode> <string> <pattern>\n"
            "modes: wildmatch, iwildmatch, pathmatch, fnmatch");
    }

    bool match = false;

    if (!strcmp(argv[1], "wildmatch"))
        match = wild::match(argv[3], argv[2]);
    else if (!strcmp(argv[1], "iwildmatch"))
        match = wild::match(argv[3], argv[2], wild::WILDSTAR | wild::CASEFOLD);
    else if (!strcmp(argv[1], "pathmatch") || !strcmp(argv[1], "fnmatch"))
        match = wild::match(argv[3], argv[2], wild::FNMATCH);

    return (match) ? 0 : 1;
}
