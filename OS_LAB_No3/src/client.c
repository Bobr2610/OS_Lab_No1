#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>

#define BUFFER_SIZE 4096
#define SHM_SIZE (BUFFER_SIZE + 8)

static size_t string_length(const char *text) {
	size_t length = 0;
	while (text[length] != '\0') ++length;
	return length;
}

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


static bool parse_and_sum(char *line, double *result) {
	char *cursor = line;
	double total = 0.0;
	bool has_value = false;
	while (*cursor != '\0') {
		while (*cursor == ' ' || *cursor == '\t') ++cursor;
		
		if (*cursor == '\0') break;
		errno = 0;
		char *next = NULL;
		// string -> double
		double value = strtod(cursor, &next);
		if (cursor == next || errno == ERANGE) return false;

		total += value;
		has_value = true;
		cursor = next;
	}
	if (!has_value) return false;
	*result = total;
	return true;
}
// double -> string
static size_t format_double(double value, char *buffer, size_t capacity) {
	if (capacity == 0) return 0;
	size_t index = 0;
	if (value < 0.0) {
		buffer[index++] = '-';
		value = -value;
		if (index >= capacity) return 0;
	}

	long long integer_part = (long long)value;
	double fractional_part = value - (double)integer_part;

	char digits[32];
	size_t digit_count = 0;
	do {
		digits[digit_count++] = (char)('0' + (integer_part % 10));
		integer_part /= 10;
	} while (integer_part > 0 && digit_count < sizeof(digits));

	while (digit_count > 0 && index < capacity) {
		buffer[index++] = digits[--digit_count];
	}
	if (index >= capacity) {
		return 0;
	}

	buffer[index++] = '.';
	for (int i = 0; i < 6 && index < capacity; ++i) {
		fractional_part *= 10.0;
		int digit = (int)fractional_part;
		buffer[index++] = (char)('0' + digit);
		fractional_part -= digit;
	}

	size_t end = index;
	while (end > 0 && buffer[end - 1] == '0') {
		--end;
	}
	if (end > 0 && buffer[end - 1] == '.') ++end;

	if (end >= capacity) return 0;
	buffer[end] = '\0';
	return end;
}

int main(int argc, char **argv) {
	// Check arguments: filename, shm_p2c, shm_c2p, sem_pw, sem_cr, sem_cw, sem_pr
	if (argc < 8) {
		fail("error: insufficient arguments\n");
	}

	const char *filename = argv[1];
	const char *shm_parent_to_child_name = argv[2];
	const char *shm_child_to_parent_name = argv[3];
	const char *sem_parent_write_name = argv[4];
	const char *sem_child_read_name = argv[5];
	const char *sem_child_write_name = argv[6];
	const char *sem_parent_read_name = argv[7];

	// Open shared memory segments
	int shm_p2c_fd = shm_open(shm_parent_to_child_name, O_RDWR, 0);
	if (shm_p2c_fd == -1) {
		fail("error: failed to open parent-to-child shared memory\n");
	}
	void *shm_p2c = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_p2c_fd, 0);
	if (shm_p2c == MAP_FAILED) {
		close(shm_p2c_fd);
		fail("error: failed to map parent-to-child shared memory\n");
	}
	close(shm_p2c_fd);

	int shm_c2p_fd = shm_open(shm_child_to_parent_name, O_RDWR, 0);
	if (shm_c2p_fd == -1) {
		munmap(shm_p2c, SHM_SIZE);
		fail("error: failed to open child-to-parent shared memory\n");
	}
	void *shm_c2p = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_c2p_fd, 0);
	if (shm_c2p == MAP_FAILED) {
		close(shm_c2p_fd);
		munmap(shm_p2c, SHM_SIZE);
		fail("error: failed to map child-to-parent shared memory\n");
	}
	close(shm_c2p_fd);

	// Open semaphores
	sem_t *sem_parent_write = sem_open(sem_parent_write_name, 0);
	sem_t *sem_child_read = sem_open(sem_child_read_name, 0);
	sem_t *sem_child_write = sem_open(sem_child_write_name, 0);
	sem_t *sem_parent_read = sem_open(sem_parent_read_name, 0);

	if (sem_parent_write == SEM_FAILED || sem_child_read == SEM_FAILED ||
	    sem_child_write == SEM_FAILED || sem_parent_read == SEM_FAILED) {
		munmap(shm_p2c, SHM_SIZE);
		munmap(shm_c2p, SHM_SIZE);
		if (sem_parent_write != SEM_FAILED) sem_close(sem_parent_write);
		if (sem_child_read != SEM_FAILED) sem_close(sem_child_read);
		if (sem_child_write != SEM_FAILED) sem_close(sem_child_write);
		if (sem_parent_read != SEM_FAILED) sem_close(sem_parent_read);
		fail("error: failed to open semaphores\n");
	}

	// O_WRONLY - write only, O_CREAT - create if not exists, O_TRUNC - truncate if exists, 0600 - R & W
	int file = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (file == -1) {
		munmap(shm_p2c, SHM_SIZE);
		munmap(shm_c2p, SHM_SIZE);
		sem_close(sem_parent_write);
		sem_close(sem_child_read);
		sem_close(sem_child_write);
		sem_close(sem_parent_read);
		fail("error: failed to open file\n");
	}

	char line[BUFFER_SIZE];
	bool should_continue = true;

	while(should_continue) {
		// Wait for parent to write
		if (sem_wait(sem_parent_write) == -1) {
			fail("error: failed to wait sem_parent_write\n");
		}

		// Read line from shared memory (parent to child)
		size_t *size_ptr = (size_t *)shm_p2c;
		size_t line_length = *size_ptr;
		char *data_ptr = (char *)shm_p2c + sizeof(size_t);

		if (line_length == 0) {
			should_continue = false;
			// Signal that child has read
			if (sem_post(sem_child_read) == -1) {
				fail("error: failed to post sem_child_read\n");
			}
			break;
		}

		// Copy line to local buffer
		if (line_length >= BUFFER_SIZE) {
			line_length = BUFFER_SIZE - 1;
		}
		memcpy(line, data_ptr, line_length);
		line[line_length] = '\0';

		// Signal that child has read
		if (sem_post(sem_child_read) == -1) {
			fail("error: failed to post sem_child_read\n");
		}

		// Process the line
		if (line_length > 0 && line[line_length - 1] == '\n') {
			line[line_length - 1] = '\0';
		}

		double sum = 0.0;
		bool valid = parse_and_sum(line, &sum);
		
		char response[BUFFER_SIZE];
		size_t response_length = 0;

		if (!valid) {
			const char warning[] = "error: invalid input\n";
			response_length = sizeof(warning) - 1;
			memcpy(response, warning, response_length);
		} else {
			char value_buffer[128];
			size_t value_length = format_double(sum, value_buffer, sizeof(value_buffer));
			if (value_length == 0) {
				fail("error: failed to format result\n");
			}

			const char prefix[] = "sum: ";
			const char newline = '\n';
			
			// Write to file
			write_all(file, prefix, sizeof(prefix) - 1);
			write_all(file, value_buffer, value_length);
			write_all(file, &newline, 1);

			// Prepare response for parent
			size_t index = 0;
			memcpy(response + index, prefix, sizeof(prefix) - 1);
			index += sizeof(prefix) - 1;
			memcpy(response + index, value_buffer, value_length);
			index += value_length;
			response[index++] = newline;
			response_length = index;
		}

		// Write response to shared memory (child to parent)
		size_t *resp_size_ptr = (size_t *)shm_c2p;
		*resp_size_ptr = response_length;
		char *resp_data_ptr = (char *)shm_c2p + sizeof(size_t);
		if (response_length > 0) {
			memcpy(resp_data_ptr, response, response_length);
			if (response_length < SHM_SIZE - sizeof(size_t)) {
				resp_data_ptr[response_length] = '\0';
			}
		}

		// Signal that child has written
		if (sem_post(sem_child_write) == -1) {
			fail("error: failed to post sem_child_write\n");
		}

		// Wait for parent to read
		if (sem_wait(sem_parent_read) == -1) {
			fail("error: failed to wait sem_parent_read\n");
		}
	}

	if (close(file) == -1) {
		fail("error: failed to close file\n");
	}

	munmap(shm_p2c, SHM_SIZE);
	munmap(shm_c2p, SHM_SIZE);
	sem_close(sem_parent_write);
	sem_close(sem_child_read);
	sem_close(sem_child_write);
	sem_close(sem_parent_read);

	return EXIT_SUCCESS;
}