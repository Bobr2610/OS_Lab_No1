#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>

#define CHILD_PROGRAM_NAME "lab_01_child"
#define MAX_LINE_LENGTH 4096
#define SHM_SIZE (MAX_LINE_LENGTH + 8)

static size_t string_length(const char *text) {
	size_t length = 0;
	while (text[length] != '\0') ++length;
	return length;
}
// write all bytes to fd если не все записалось
static void write_all(int fd, const char *buffer, size_t length) {
	while (length > 0) {
		ssize_t written = write(fd, buffer, length);
		if (written < 0) _exit(EXIT_FAILURE);
		buffer += (size_t)written;
		length -= (size_t)written;
	}
}

static void fail(const char *message) {
	write_all(STDERR_FILENO, message, string_length(message));
	_exit(EXIT_FAILURE);
}
//
static ssize_t read_line(int fd, char *buffer, size_t capacity) {
	if (capacity == 0) return -1;

	size_t offset = 0;
	while (offset + 1 < capacity) {
		char ch;
		ssize_t bytes = read(fd, &ch, 1);
		if (bytes < 0) {
			return -1;
		}
		if (bytes == 0) break;

		buffer[offset++] = ch;
		if (ch == '\n') break;
	}
	buffer[offset] = '\0';
	return (ssize_t)offset;
}
// input
static void trim_trailing_newline(char *line) {
	size_t length = string_length(line);
	if (length == 0) {
		return;
	}
	if (line[length - 1] == '\n') line[length - 1] = '\0';
}

static void build_child_path(char *result, size_t capacity) {
	char executable_path[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", executable_path, sizeof(executable_path) - 1);
	if (len == -1){
		fail("error: failed to read /proc/self/exe\n");
	}

	executable_path[len] = '\0';

	while (len > 0 && executable_path[len] != '/') --len;
	if (len == 0) {
		fail("error: executable path is invalid\n");
	}

	executable_path[len] = '\0';

	size_t dir_length = string_length(executable_path);
	size_t name_length = string_length(CHILD_PROGRAM_NAME);
	if (dir_length + 1 + name_length + 1 > capacity) {
		fail("error: child path buffer is too small\n");
	}

	size_t index = 0;
	for (size_t i = 0; i < dir_length; ++i) result[index++] = executable_path[i];
	result[index++] = '/';
	for (size_t i = 0; i < name_length; ++i) result[index++] = CHILD_PROGRAM_NAME[i];

	result[index] = '\0';
}

static void generate_unique_name(char *buffer, size_t capacity, const char *prefix) {
	pid_t pid = getpid();
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
		fail("error: failed to get time\n");
	}
	
	int written = snprintf(buffer, capacity, "%s_%d_%ld_%ld", prefix, pid, ts.tv_sec, ts.tv_nsec);
	if (written < 0 || (size_t)written >= capacity) {
		fail("error: failed to generate unique name\n");
	}
}
// write line to output_fd if line does not end with \n
static void forward_line(int output_fd, const char *line) {
	size_t length = string_length(line);
	if (length == 0 || line[length - 1] != '\n') {
		write_all(output_fd, line, length);
		write_all(output_fd, "\n", 1);
		return;
	}
	write_all(output_fd, line, length);
}

int main(void) {
	char filename[MAX_LINE_LENGTH];
	ssize_t filename_len = read_line(STDIN_FILENO, filename, sizeof(filename));
	if (filename_len <= 0) {
		fail("error: failed to read filename\n");
	}

	trim_trailing_newline(filename);
	if (string_length(filename) == 0) {
		fail("error: filename must not be empty\n");
	}

	// Generate unique names for shared memory and semaphores
	char shm_parent_to_child_name[256];
	char shm_child_to_parent_name[256];
	char sem_parent_write_name[256];
	char sem_child_read_name[256];
	char sem_child_write_name[256];
	char sem_parent_read_name[256];

	generate_unique_name(shm_parent_to_child_name, sizeof(shm_parent_to_child_name), "/shm_p2c");
	generate_unique_name(shm_child_to_parent_name, sizeof(shm_child_to_parent_name), "/shm_c2p");
	generate_unique_name(sem_parent_write_name, sizeof(sem_parent_write_name), "/sem_pw");
	generate_unique_name(sem_child_read_name, sizeof(sem_child_read_name), "/sem_cr");
	generate_unique_name(sem_child_write_name, sizeof(sem_child_write_name), "/sem_cw");
	generate_unique_name(sem_parent_read_name, sizeof(sem_parent_read_name), "/sem_pr");

	// Create shared memory for parent to child communication
	int shm_p2c_fd = shm_open(shm_parent_to_child_name, O_CREAT | O_RDWR, 0600);
	if (shm_p2c_fd == -1) {
		fail("error: failed to create parent-to-child shared memory\n");
	}
	if (ftruncate(shm_p2c_fd, SHM_SIZE) == -1) {
		shm_unlink(shm_parent_to_child_name);
		fail("error: failed to truncate parent-to-child shared memory\n");
	}
	void *shm_p2c = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_p2c_fd, 0);
	if (shm_p2c == MAP_FAILED) {
		close(shm_p2c_fd);
		shm_unlink(shm_parent_to_child_name);
		fail("error: failed to map parent-to-child shared memory\n");
	}
	close(shm_p2c_fd);

	// Create shared memory for child to parent communication
	int shm_c2p_fd = shm_open(shm_child_to_parent_name, O_CREAT | O_RDWR, 0600);
	if (shm_c2p_fd == -1) {
		munmap(shm_p2c, SHM_SIZE);
		shm_unlink(shm_parent_to_child_name);
		fail("error: failed to create child-to-parent shared memory\n");
	}
	if (ftruncate(shm_c2p_fd, SHM_SIZE) == -1) {
		munmap(shm_p2c, SHM_SIZE);
		close(shm_c2p_fd);
		shm_unlink(shm_parent_to_child_name);
		shm_unlink(shm_child_to_parent_name);
		fail("error: failed to truncate child-to-parent shared memory\n");
	}
	void *shm_c2p = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_c2p_fd, 0);
	if (shm_c2p == MAP_FAILED) {
		munmap(shm_p2c, SHM_SIZE);
		close(shm_c2p_fd);
		shm_unlink(shm_parent_to_child_name);
		shm_unlink(shm_child_to_parent_name);
		fail("error: failed to map child-to-parent shared memory\n");
	}
	close(shm_c2p_fd);

	// Create semaphores
	sem_t *sem_parent_write = sem_open(sem_parent_write_name, O_CREAT, 0600, 0);
	sem_t *sem_child_read = sem_open(sem_child_read_name, O_CREAT, 0600, 0);
	sem_t *sem_child_write = sem_open(sem_child_write_name, O_CREAT, 0600, 0);
	sem_t *sem_parent_read = sem_open(sem_parent_read_name, O_CREAT, 0600, 0);

	if (sem_parent_write == SEM_FAILED || sem_child_read == SEM_FAILED ||
	    sem_child_write == SEM_FAILED || sem_parent_read == SEM_FAILED) {
		munmap(shm_p2c, SHM_SIZE);
		munmap(shm_c2p, SHM_SIZE);
		shm_unlink(shm_parent_to_child_name);
		shm_unlink(shm_child_to_parent_name);
		if (sem_parent_write != SEM_FAILED) sem_close(sem_parent_write);
		if (sem_child_read != SEM_FAILED) sem_close(sem_child_read);
		if (sem_child_write != SEM_FAILED) sem_close(sem_child_write);
		if (sem_parent_read != SEM_FAILED) sem_close(sem_parent_read);
		sem_unlink(sem_parent_write_name);
		sem_unlink(sem_child_read_name);
		sem_unlink(sem_child_write_name);
		sem_unlink(sem_parent_read_name);
		fail("error: failed to create semaphores\n");
	}

	// Fork child process
	pid_t child = fork();
	if (child == -1) {
		munmap(shm_p2c, SHM_SIZE);
		munmap(shm_c2p, SHM_SIZE);
		shm_unlink(shm_parent_to_child_name);
		shm_unlink(shm_child_to_parent_name);
		sem_close(sem_parent_write);
		sem_close(sem_child_read);
		sem_close(sem_child_write);
		sem_close(sem_parent_read);
		sem_unlink(sem_parent_write_name);
		sem_unlink(sem_child_read_name);
		sem_unlink(sem_child_write_name);
		sem_unlink(sem_parent_read_name);
		fail("error: failed to fork\n");
	}

	if (child == 0) {
		// Close semaphores in child (will reopen them)
		sem_close(sem_parent_write);
		sem_close(sem_child_read);
		sem_close(sem_child_write);
		sem_close(sem_parent_read);
		munmap(shm_p2c, SHM_SIZE);
		munmap(shm_c2p, SHM_SIZE);

		char child_path[PATH_MAX];
		build_child_path(child_path, sizeof(child_path));
		// Pass filename and shared memory/semaphore names to child process
		char *const args[] = {
			CHILD_PROGRAM_NAME,
			filename,
			shm_parent_to_child_name,
			shm_child_to_parent_name,
			sem_parent_write_name,
			sem_child_read_name,
			sem_child_write_name,
			sem_parent_read_name,
			NULL
		};
		execv(child_path, args);
		fail("error: exec failed\n");
	}

	// Parent process: communicate via shared memory
	char line_buffer[MAX_LINE_LENGTH];
	while(true) {
		ssize_t line_length = read_line(STDIN_FILENO, line_buffer, sizeof(line_buffer));
		if (line_length == -1) {
			fail("error: failed to read input line\n");
		}

		if (line_length == 0 || line_buffer[0] == '\n') {
			// Send termination signal to child
			size_t *size_ptr = (size_t *)shm_p2c;
			*size_ptr = 0;
			
			// Signal that parent has written (termination signal)
			if (sem_post(sem_parent_write) == -1) {
				fail("error: failed to post sem_parent_write\n");
			}
			
			// Wait for child to read termination signal
			if (sem_wait(sem_child_read) == -1) {
				fail("error: failed to wait sem_child_read\n");
			}
			break;
		}

		// Write line to shared memory (parent to child)
		size_t *size_ptr = (size_t *)shm_p2c;
		*size_ptr = (size_t)line_length;
		char *data_ptr = (char *)shm_p2c + sizeof(size_t);
		memcpy(data_ptr, line_buffer, (size_t)line_length);
		data_ptr[line_length] = '\0';

		// Signal that parent has written
		if (sem_post(sem_parent_write) == -1) {
			fail("error: failed to post sem_parent_write\n");
		}

		// Wait for child to read
		if (sem_wait(sem_child_read) == -1) {
			fail("error: failed to wait sem_child_read\n");
		}

		// Wait for child to write response
		if (sem_wait(sem_child_write) == -1) {
			fail("error: failed to wait sem_child_write\n");
		}

		// Read response from shared memory (child to parent)
		size_t *resp_size_ptr = (size_t *)shm_c2p;
		size_t resp_size = *resp_size_ptr;
		char *resp_data_ptr = (char *)shm_c2p + sizeof(size_t);
		
		if (resp_size > 0 && resp_size < MAX_LINE_LENGTH) {
			forward_line(STDOUT_FILENO, resp_data_ptr);
		}

		// Signal that parent has read
		if (sem_post(sem_parent_read) == -1) {
			fail("error: failed to post sem_parent_read\n");
		}
	}

	// Cleanup
	munmap(shm_p2c, SHM_SIZE);
	munmap(shm_c2p, SHM_SIZE);
	shm_unlink(shm_parent_to_child_name);
	shm_unlink(shm_child_to_parent_name);
	sem_close(sem_parent_write);
	sem_close(sem_child_read);
	sem_close(sem_child_write);
	sem_close(sem_parent_read);
	sem_unlink(sem_parent_write_name);
	sem_unlink(sem_child_read_name);
	sem_unlink(sem_child_write_name);
	sem_unlink(sem_parent_read_name);

	// Wait for child process to finish
	int status = 0;
	if (waitpid(child, &status, 0) == -1) {
		fail("error: waitpid failed\n");
	}
	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	} else {
		return EXIT_FAILURE;
	}
}