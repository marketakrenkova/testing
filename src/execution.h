#ifndef CUT_EXECUTION_H
#define CUT_EXECUTION_H

#ifndef CUT_MAIN
#error "cannot be standalone"
#endif

# ifdef __cplusplus
} // extern C

#  include <stdexcept>
#  include <typeinfo>
#  include <string>

CUT_PRIVATE void cut_ExceptionBypass(int testId, int subtest) {
    cut_RedirectIO();
    if (setjmp(cut_executionPoint))
        goto leave;
    try {
        int counter = 0;
        cut_unitTests.tests[testId].instance(&counter, subtest);
        cut_SendOK(counter);
    } catch (const std::exception &e) {
        std::string name = typeid(e).name();
        cut_StopException(name.c_str(), e.what());
    } catch (...) {
        cut_StopException("unknown type", "(empty message)");
    }
leave:
    cut_ResumeIO();
}

extern "C" {
# else
CUT_PRIVATE void cut_ExceptionBypass(int testId, int subtest) {
    cut_RedirectIO();
    if (setjmp(cut_executionPoint))
        goto leave;
    int counter = 0;
    cut_unitTests.tests[testId].instance(&counter, subtest);
    cut_SendOK(counter);
leave:
    cut_ResumeIO();
}
# endif


CUT_PRIVATE int cut_SkipUnit(int testId) {
    if (cut_arguments.testId >= 0)
        return testId != cut_arguments.testId;
    if (!cut_arguments.matchSize)
        return 0;
    const char *name = cut_unitTests.tests[testId].name;
    for (int i = 0; i < cut_arguments.matchSize; ++i) {
        if (strstr(name, cut_arguments.match[i]))
            return 0;
    }
    return 1;
}

CUT_PRIVATE const char *cut_GetStatus(const struct cut_UnitResult *result, int *length) {
    static const char *ok = "\e[1;32mOK\e[0m";
    static const char *basicOk = "OK";
    static const char *fail = "\e[1;31mFAIL\e[0m";
    static const char *basicFail = "FAIL";
    static const char *internalFail = "\e[1;33mINTERNAL ERROR\e[0m";
    static const char *basicInternalFail = "INTERNAL ERROR";

    if (result->returnCode == cut_FATAL_EXIT) {
        *length = strlen(basicInternalFail);
        return cut_arguments.noColor ? basicInternalFail : internalFail;
    }
    if (result->failed) {
        *length = strlen(basicFail);
        return cut_arguments.noColor ? basicFail : fail;
    }
    *length = strlen(basicOk);
    return cut_arguments.noColor ? basicOk : ok;
}

CUT_PRIVATE const char *cut_ShortPath(const char *path) {
    enum { MAX_PATH = 80 };
    static char shortenedPath[MAX_PATH + 1];
    char *cursor = shortenedPath;
    const char *dots = "...";
    const size_t dotsLength = strlen(dots);
    size_t pathLength = strlen(path);
    if (cut_arguments.shortPath < 0 || pathLength <= cut_arguments.shortPath)
        return path;
    if (cut_arguments.shortPath > MAX_PATH)
        cut_arguments.shortPath = MAX_PATH;
    
    int fullName = 0;
    const char *end = path + strlen(path);
    const char *name = end;
    for (; end - name < cut_arguments.shortPath && path < name; --name) {
        if (*name == '/' || *name == '\\') {
            fullName = 1;
            break;
        }
    }
    size_t consumed = (end - name) + dotsLength;
    if (consumed < cut_arguments.shortPath) {
        size_t remaining = cut_arguments.shortPath - consumed;
        size_t firstPart = remaining - remaining / 2;
        strncpy(cursor, path, firstPart);
        cursor += firstPart;
        strcpy(cursor, dots);
        cursor += dotsLength;
        remaining -= firstPart;
        name -= remaining;
    }
    strcpy(cursor, name);
    return shortenedPath;
}

CUT_PRIVATE void cut_PrintResult(int base, int subtest, const struct cut_UnitResult *result) {
    static const char *shortIndent = "    ";
    static const char *longIndent = "        ";
    int statusLength;
    const char *status = cut_GetStatus(result, &statusLength);
    int lastPosition = 80 - 1 - statusLength;
    int extended = 0;

    const char *indent = shortIndent;
    if (result->name && subtest) {
        if (subtest < 0)
            putc('\n', cut_output);
        if (result->number <= 1)
            lastPosition -= fprintf(cut_output, "%s%s", indent, result->name);
        else {
            lastPosition -= fprintf(cut_output, "%s", indent);
            int length = strlen(result->name);
            for (int i = 0; i < length; ++i)
                putc(' ', cut_output);
            lastPosition -= length;
        }
        if (result->number)
            lastPosition -= fprintf(cut_output, " #%d", result->number);
        indent = longIndent;
    } else {
        lastPosition -= base;
    }
    if (!subtest)
        extended = 1;

    for (int i = 0; i < lastPosition; ++i) {
        putc('.', cut_output);
    }
    fprintf(cut_output, "%s\n", status);
    if (result->failed) {
        for (const struct cut_Info *current = result->check; current; current = current->next) {
            fprintf(cut_output, "%scheck '%s' (%s:%d)\n", indent, current->message,
                    cut_ShortPath(current->file), current->line);
        }
        if (result->timeouted)
            fprintf(cut_output, "%stimeouted (%d s)\n", indent, cut_arguments.timeout);
        else if (result->signal)
            fprintf(cut_output, "%ssignal code: %d\n", indent, result->signal);
        if (result->returnCode)
            fprintf(cut_output, "%sreturn code: %d\n", indent, result->returnCode);
        if (result->statement && result->file && result->line)
            fprintf(cut_output, "%sassert '%s' (%s:%d)\n", indent,
                    result->statement, cut_ShortPath(result->file), result->line);
        if (result->exceptionType && result->exceptionMessage)
            fprintf(cut_output, "%sexception %s: %s\n", indent,
                    result->exceptionType, result->exceptionMessage);
        extended = 1;
    }
    if (result->debug) {
        fprintf(cut_output, "%sdebug messages:\n", indent);
        extended = 1;
    }
    for (const struct cut_Info *current = result->debug; current; current = current->next) {
        fprintf(cut_output, "%s  %s (%s:%d)\n", indent, current->message,
                cut_ShortPath(current->file), current->line);
    }
    if (extended)
        fprintf(cut_output, "\n");
    fflush(cut_output);
}

CUT_PRIVATE void cut_RunUnitForkless(int testId, int subtest, struct cut_UnitResult *result) {
    cut_ExceptionBypass(testId, subtest);
    cut_PipeReader(result);
    cut_ResetLocalMessage();
    result->returnCode = 0;
    result->signal = 0;
}


CUT_PRIVATE int cut_Runner(int argc, char **argv) {
    cut_output = stdout;
    cut_ParseArguments(argc, argv);

    cut_PreRun();

    if (cut_arguments.output) {
        cut_output = fopen(cut_arguments.output, "w");
        if (!cut_output)
            cut_ErrorExit("cannot open file %s for writing", cut_arguments.output);
    }

    if (cut_arguments.help)
        return cut_Help();

    void (*unitRunner)(int, int, struct cut_UnitResult *) =
        cut_arguments.noFork ? cut_RunUnitForkless : cut_RunUnit;

    int failed = 0;
    int executed = 0;
    for (int i = 0; i < cut_unitTests.size; ++i) {
        if (cut_SkipUnit(i))
            continue;
        ++executed;
        int base = fprintf(cut_output, "[%3i] %s", executed, cut_unitTests.tests[i].name);
        fflush(cut_output);
        int subtests = 1;
        if (cut_arguments.subtestId > 0)
            subtests = cut_arguments.subtestId;
        int subtestFailure = 0;
        int firstSubtest = -1;
        for (int subtest = 1; subtest <= subtests; ++subtest) {
            if (cut_arguments.subtestId >= 0 && cut_arguments.subtestId != subtest)
                continue;
            struct cut_UnitResult result;
            memset(&result, 0, sizeof(result));
            unitRunner(i, subtest, &result);
            if (result.failed)
                ++subtestFailure;
            FILE *emergencyLog = fopen(cut_emergencyLog, "r");
            if (emergencyLog) {
                char buffer[512] = {0,};
                fread(buffer, 512, 1, emergencyLog);
                if (*buffer) {
                    result.statement = (char *)malloc(strlen(buffer) + 1);
                    strcpy(result.statement, buffer);
                }
                fclose(emergencyLog);
                remove(cut_emergencyLog);
            }
            cut_PrintResult(base, subtest * firstSubtest, &result);
            cut_CleanMemory(&result);
            if (result.subtests > subtests)
                subtests = result.subtests;
            firstSubtest = 1;
        }
        if (subtests > 1) {
            base = fprintf(cut_output, "[%3i] %s (overall)", executed, cut_unitTests.tests[i].name);
            struct cut_UnitResult result;
            memset(&result, 0, sizeof(result));
            result.failed = subtestFailure;
            cut_PrintResult(base, 0, &result);
        }
        if (subtestFailure)
            ++failed;
    }
    fprintf(cut_output,
            "\nSummary:\n"
            "  tests:     %3i\n"
            "  succeeded: %3i\n"
            "  skipped:   %3i\n"
            "  failed:    %3i\n",
            cut_unitTests.size,
            executed - failed,
            cut_unitTests.size - executed,
            failed);
    free(cut_unitTests.tests);
    free(cut_arguments.match);
    if (cut_arguments.output)
        fclose(cut_output);
    return failed;
}

#endif // CUT_EXECUTION_H