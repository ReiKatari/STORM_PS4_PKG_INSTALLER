// Absolute minimum test - no API calls at all

// Override libc's broken _init/_fini
void _init(void) {
    // Do nothing
}

void _fini(void) {
    // Do nothing
}

int main(void) {
    // Volatile to prevent compiler optimization
    volatile int x = 0;
    while (1) {
        x++;
    }
    return 0;
}
