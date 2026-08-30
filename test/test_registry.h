#pragma once

#include <cstdio>
#include <cstring>
#include <vector>

extern int sTestsPassed;
extern int sTestsFailed;

struct IoHomeTestCase
{
    const char *name;
    void (*function)();
};

class IoHomeTestRegistry
{
  public:
    static void add(const char *name, void (*function)())
    {
        tests().push_back({name, function});
    }

    static int run(int argc, char **argv)
    {
        const char *suite = "all";
        const char *filter = nullptr;
        bool listOnly = false;

        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--suite") == 0 && i + 1 < argc)
                suite = argv[++i];
            else if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc)
                filter = argv[++i];
            else if (std::strcmp(argv[i], "--list") == 0)
                listOnly = true;
            else if (std::strcmp(argv[i], "--help") == 0)
            {
                std::printf("Usage: %s [--suite protocol|controller|exchange|all] [--filter text] [--list]\n", argv[0]);
                return 0;
            }
            else
            {
                std::fprintf(stderr, "Unknown test-runner option: %s\n", argv[i]);
                return 2;
            }
        }

        if (!isKnownSuite(suite))
        {
            std::fprintf(stderr, "Unknown test suite: %s\n", suite);
            return 2;
        }

        size_t selected = 0;
        for (const auto &test : tests())
        {
            if (!matches(test, suite, filter))
                continue;

            ++selected;
            if (listOnly)
                std::printf("%s\t%s\n", suiteFor(test.name), test.name);
        }

        if (selected == 0)
        {
            std::fprintf(stderr, "No tests selected for suite '%s'.\n", suite);
            return 2;
        }

        if (listOnly)
            return 0;

        sTestsPassed = 0;
        sTestsFailed = 0;
        std::printf("Running %zu %s test(s)%s%s...\n", selected, suite,
                    filter ? " matching '" : "", filter ? filter : "");
        if (filter)
            std::printf("'\n");

        for (const auto &test : tests())
        {
            if (!matches(test, suite, filter))
                continue;

            const int failuresBefore = sTestsFailed;
            test.function();
            if (sTestsFailed == failuresBefore)
            {
                ++sTestsPassed;
                std::printf("[PASS] %s\n", test.name);
            }
            else
                std::printf("[FAIL] %s\n", test.name);
        }

        std::printf("\n%d passed, %d failed\n", sTestsPassed, sTestsFailed);
        return sTestsFailed == 0 ? 0 : 1;
    }

  private:
    static std::vector<IoHomeTestCase> &tests()
    {
        static std::vector<IoHomeTestCase> registry;
        return registry;
    }

    static bool isKnownSuite(const char *suite)
    {
        return std::strcmp(suite, "all") == 0 || std::strcmp(suite, "protocol") == 0 ||
               std::strcmp(suite, "controller") == 0 || std::strcmp(suite, "exchange") == 0;
    }

    static const char *suiteFor(const char *name)
    {
        if (std::strstr(name, "pairing") || std::strstr(name, "key_exchange") ||
            std::strstr(name, "key_extract") || std::strstr(name, "integration_"))
            return "exchange";
        if (std::strncmp(name, "controller_", 11) == 0 || std::strncmp(name, "gateway_", 8) == 0 ||
            std::strncmp(name, "channel_", 8) == 0 || std::strncmp(name, "retry_", 6) == 0 ||
            std::strncmp(name, "byte_vector_controller_", 23) == 0)
            return "controller";
        return "protocol";
    }

    static bool matches(const IoHomeTestCase &test, const char *suite, const char *filter)
    {
        return (std::strcmp(suite, "all") == 0 || std::strcmp(suiteFor(test.name), suite) == 0) &&
               (!filter || std::strstr(test.name, filter));
    }
};

class IoHomeTestRegistrar
{
  public:
    IoHomeTestRegistrar(const char *name, void (*function)())
    {
        IoHomeTestRegistry::add(name, function);
    }
};
