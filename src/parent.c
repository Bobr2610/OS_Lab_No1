#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHILD_PROGRAM_NAME "child.c"
#define MAX_LINE_LENGTH 4096

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
	if (len == -1) fail("error: failed to read /proc/self/exe\n");

	executable_path[len] = '\0';

	while (len > 0 && executable_path[len] != '/') --len;
	if (len == 0) fail("error: executable path is invalid\n");

	executable_path[len] = '\0';

	size_t dir_length = string_length(executable_path);
	size_t name_length = string_length(CHILD_PROGRAM_NAME);
	if (dir_length + 1 + name_length + 1 > capacity) fail("error: child path buffer is too small\n");

	size_t index = 0;
	for (size_t i = 0; i < dir_length; ++i) result[index++] = executable_path[i];
	result[index++] = '/';
	for (size_t i = 0; i < name_length; ++i) result[index++] = CHILD_PROGRAM_NAME[i];

	result[index] = '\0';
}

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
	if (filename_len <= 0) fail("error: failed to read filename\n");

	trim_trailing_newline(filename);
	if (string_length(filename) == 0) fail("error: filename must not be empty\n");

	int parent_to_child[2];
	if (pipe(parent_to_child) == -1) fail("error: failed to create pipe\n");

	int child_to_parent[2];
	if (pipe(child_to_parent) == -1) fail("error: failed to create pipe\n");
	
	pid_t child = fork();
	if (child == -1) fail("error: failed to fork\n");

	if (child == 0) {
		if (close(parent_to_child[1]) == -1 || close(child_to_parent[0]) == -1) fail("error: failed to close descriptors in child\n");

		if (dup2(parent_to_child[0], STDIN_FILENO) == -1) fail("error: failed to redirect stdin\n");

		if (dup2(child_to_parent[1], STDOUT_FILENO) == -1) fail("error: failed to redirect stdout\n");
        
		if (close(parent_to_child[0]) == -1 || close(child_to_parent[1]) == -1) fail("error: failed to close redundant descriptors\n");

		char child_path[PATH_MAX];
		build_child_path(child_path, sizeof(child_path));

		char *const args[] = {CHILD_PROGRAM_NAME, filename, NULL};
		execv(child_path, args);
		fail("error: exec failed\n");
	}

	if (close(parent_to_child[0]) == -1 || close(child_to_parent[1]) == -1) fail("error: failed to close descriptors in parent\n");

	char line_buffer[MAX_LINE_LENGTH];
	while(true) {
		ssize_t line_length = read_line(STDIN_FILENO, line_buffer, sizeof(line_buffer));
		if (line_length < 0) fail("error: failed to read input line\n");

		if (line_length == 0) break;

		if (line_buffer[0] == '\n') break;

		write_all(parent_to_child[1], line_buffer, (size_t)line_length);

		char response[MAX_LINE_LENGTH];
		ssize_t response_length = read_line(child_to_parent[0], response, sizeof(response));
		if (response_length <= 0) fail("error: child response failed\n");
		forward_line(STDOUT_FILENO, response);
	}

	if (close(parent_to_child[1]) == -1 || close(child_to_parent[0]) == -1) fail("error: failed to close pipes\n");

	int status = 0;
	if (waitpid(child, &status, 0) == -1) {
		fail("error: waitpid failed\n");
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}