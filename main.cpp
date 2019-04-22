#include <iostream>
#include <vector>
#include <unistd.h>
#include <cerrno>
#include <memory>
#include <string.h>
#include <sys/wait.h>
#include <assert.h>
#include <sys/mman.h>
#include <iomanip>
#include <fstream>
#include "InterpreterJIT.h"


using namespace std;


const size_t TAPE_SIZE = 1000000;


void printUsage() {
    cout << "Usage ./os-jit <code file>\n"
            "   Interpret Brainfuck code with Just-In-Time optimization\n"
            "   Interpreted program reads symbols from stdin and writes to stdout\n";
}


int main(int argc, char *argv[]) {
    InterpreterJIT interpreter(TAPE_SIZE, STDIN_FILENO, STDOUT_FILENO);

    if (argc != 2) {
        printUsage();
        return 0;
    }

    ifstream codeFile(argv[1]);
    char c;
    while (codeFile >> c) {
        interpreter.applyCommand(c);
    }
}
