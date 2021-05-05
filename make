gcc -Wall -Wno-unused-parameter -Wextra -pedantic -pthread pc-rt-common.c pc-rt-start.c -o pc-rt-start -lrt
gcc -Wall -Wno-unused-parameter -Wextra -pedantic -pthread pc-rt-common.c pc-rt-stop.c -o pc-rt-stop -lrt
gcc -Wall -Wno-unused-parameter -Wextra -pedantic -pthread logger.c pc-rt-common.c pc-rt-prod.c -o pc-rt-prod -lrt
gcc -Wall -Wno-unused-parameter -Wextra -pedantic -pthread logger.c pc-rt-common.c pc-rt-cons.c -o pc-rt-cons -lrt
