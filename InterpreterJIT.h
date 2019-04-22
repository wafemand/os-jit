#include <utility>

#pragma once


#include <vector>
#include <cstdint>
#include <bits/mman.h>
#include <sys/mman.h>
#include <unistd.h>


class CompilerException : public std::exception {
    std::string msg;
public:
    explicit CompilerException(std::string const &msg) : msg("Compiler exception:\n" + msg) {}

    char const *what() const noexcept override {
        return msg.c_str();
    }
};


class Compiler {
    using byteVector = std::vector<uint8_t>;

    byteVector prefix = {
            0x55, 0x48,       // push   rbp
            0x89, 0xe5,       // mov    rbp,rsp
            0x48, 0xBB,       // mov    rbx,<headPos>
            0, 0, 0, 0, 0, 0, 0, 0 // <headPos> will be set in runLastCycle()
    };

    byteVector suffix = {
            0x48, 0x89, 0xD8, // mov rax,rbx
            0x5d,             // pop %rbp
            0xc3              // retq
    };

    byteVector body;
    std::vector<size_t> openIfs;
    uint8_t *tapePtr;
    int fdIn;
    int fdOut;
    size_t lastCycle = 0;

    template<typename T>
    byteVector toCode(T addr) {
        byteVector res(sizeof(T));
        for (int i = 0; i < sizeof(T); i++) {
            res[i] = (0xffu & (addr >> (i * 8u)));
        }
        return res;
    }

    template<typename T>
    void addAddress(T addr) {
        for (auto b : toCode(addr)) {
            body.push_back(b);
        }
    }


    void addCode(const byteVector &bytes) {
        for (auto b : bytes) {
            body.push_back(b);
        }
    }

    void throwWithErrno(std::string const &msg) {
        std::string error = strerror(errno);
        throw CompilerException(msg + error);
    }

public:
    Compiler(uint8_t *tapePtr, int fdIn, int fdOut)
            : tapePtr(tapePtr), fdIn(fdIn), fdOut(fdOut) {
    }

    void _left_() {
        addCode({0x48, 0xFF, 0xCB}); // dec rbx
    }

    void _right_() {
        addCode({0x48, 0xFF, 0xC3}); // inc rbx
    }

    void _inc_() {
        addCode({0xFE, 0x03}); // inc byte ptr [rbx]
    }

    void _dec_() {
        addCode({0xFE, 0x0B}); // dec byte ptr [rbx]
    }

    void _print_() {
        addCode({0x48, 0x89, 0xDE}); // mov rsi,rbx ; message to write
        addCode({0x48, 0xC7, 0xC7}); // mov rdi, <fdOut>
        addAddress(fdOut); // file descriptor
        addCode({0x48, 0xC7, 0xC2, 0x01, 0x00, 0x00, 0x00}); // mov rdx,1 ; message length
        addCode({0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00}); // mov rax,1 ; system call number (sys_write)
        addCode({0x0F, 0x05}); // syscall
    }

    void _read_() {
        addCode({0x48, 0x89, 0xDE}); // mov rsi,rbx ; where to read
        addCode({0x48, 0xC7, 0xC7}); // mov rdi,<fdIn>
        addAddress(fdIn); // file descriptor
        addCode({0x48, 0xC7, 0xC2, 0x01, 0x00, 0x00, 0x00}); // mov rdx,1 ; message length
        addCode({0x48, 0xC7, 0xC0, 0x00, 0x00, 0x00, 0x00}); // mov rax,0 ; system call number (sys_write)
        addCode({0x0F, 0x05}); // syscall
    }

    void _cycleBegin_() {
        addCode({0x80, 0x3B, 0x00}); // cmp byte ptr[ebx], 0
        addCode({0x0F, 0x84});       // je <end of cycle>
        addAddress<uint32_t>(0);       // it will be set in _cycleEnd_()
        openIfs.push_back(body.size());
    }

    void _cycleEnd_() {
        uint32_t diff = body.size() - openIfs.back();
        size_t jeAddrPos = openIfs.back() - 4;
        openIfs.pop_back();

        diff += 1 + 4; // jmp + rel in _cycleEnd_()

        auto code = toCode(diff);
        copy(code.begin(), code.end(), body.begin() + jeAddrPos);

        diff += 3 + 2 + 4; // cmp + je + rel in _cycleBegin_()

        addCode({0xE9}); // jmp <-diff>
        addAddress(-diff);
        lastCycle = body.size() - diff;
    }

    size_t runLastCycle(size_t headPos) { // returns new head position
        size_t lastCycleSize = body.size() - lastCycle;
        size_t code_size = prefix.size() + lastCycleSize + suffix.size();

        auto code = toCode((uint64_t) tapePtr + headPos);
        copy(code.begin(), code.end(), prefix.end() - code.size());

        uint8_t *code_ptr = (uint8_t *) mmap(
                nullptr,
                code_size,
                PROT_WRITE | PROT_READ,
                MAP_ANONYMOUS | MAP_PRIVATE,
                -1, 0);
        if (code_ptr == (uint8_t *) -1) {
            throwWithErrno("Cannot allocate memory for code.\n");
        }

        auto it = copy(prefix.begin(), prefix.end(), code_ptr);
        it = copy(body.begin() + lastCycle, body.end(), it);
        copy(suffix.begin(), suffix.end(), it);

        int ret = mprotect(code_ptr, code_size, PROT_EXEC | PROT_READ);
        if (ret == -1) {
            std::string error = strerror(errno);
            ret = munmap(code_ptr, code_size);
            if (ret == -1) {
                throw CompilerException("Cannot change permission because\n" +
                                        error + "\n"
                                        "and cannot unmap memory because\n" +
                                        strerror(errno) + "\n"
                                        "Double fail :((\n");
            } else {
                throwWithErrno("Cannot change permission for allocated memory.\n");
            }
        }

        size_t (*nativeLastCycle)();
        nativeLastCycle = (size_t (*)()) code_ptr;

        size_t newHeadPos = nativeLastCycle() - (size_t) tapePtr;

        ret = munmap(code_ptr, code_size);
        if (ret == -1) {
            throwWithErrno("Cannot unmap allocated memory.\n");
        }
        return newHeadPos;
    }
};


class InterpreterJIT {
    std::vector<uint8_t> tape;
    size_t headPos;
    int fdIn;
    int fdOut;
    Compiler compiler;
    int balance = 0;
    int skipCommandsLevel = 0;

public:
    InterpreterJIT(size_t tapeSize, int fdIn, int fdOut)
            : tape(tapeSize, 0), headPos(0),
              fdIn(fdIn), fdOut(fdOut),
              compiler(tape.data(), fdIn, fdOut) {}

    bool shouldSkip() {
        return skipCommandsLevel != 0;
    }

    void applyCommand(char c) {
        switch (c) {
            case '+':
                if (!shouldSkip()) {
                    tape[headPos]++;
                }
                compiler._inc_();
                break;
            case '-':
                if (!shouldSkip()) {
                    tape[headPos]--;
                }
                compiler._dec_();
                break;
            case '<':
                if (!shouldSkip()) {
                    headPos--;
                }
                compiler._left_();
                break;
            case '>':
                if (!shouldSkip()) {
                    headPos++;
                }
                compiler._right_();
                break;
            case '.':
                if (!shouldSkip()) {
                    write(fdOut, &tape[headPos], 1);
                }
                compiler._print_();
                break;
            case ',':
                if (!shouldSkip()) {
                    read(fdIn, &tape[headPos], 1);
                }
                compiler._read_();
                break;
            case '[':
                balance++;
                if (!shouldSkip() && tape[headPos] == 0) {
                    skipCommandsLevel = balance;
                }
                compiler._cycleBegin_();
                break;
            case ']':
                compiler._cycleEnd_();
                balance--;
                if (balance < skipCommandsLevel) {
                    skipCommandsLevel = 0;
                }
                if (!shouldSkip() && tape[headPos] != 0) {
                    headPos = compiler.runLastCycle(headPos);
                }
                break;
            default:
                break;
        }
    }
};
