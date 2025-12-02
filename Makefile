# Executable names
MASTER_TARGET := master
WORKER_TARGET := worker

# Directories
INC_DIR    := inc
BUILD_DIR  := build
MASTER_DIR := master
WORKER_DIR := worker

CC      := mpicc
CFLAGS  := -Wall -Wextra -O2 -std=gnu11 -I$(INC_DIR)
LDFLAGS :=

MASTER_SRCS := $(wildcard $(MASTER_DIR)/*.c)
WORKER_SRCS := $(wildcard $(WORKER_DIR)/*.c)

MASTER_OBJS := $(patsubst $(MASTER_DIR)/%.c,$(BUILD_DIR)/master_%.o,$(MASTER_SRCS))
WORKER_OBJS := $(patsubst $(WORKER_DIR)/%.c,$(BUILD_DIR)/worker_%.o,$(WORKER_SRCS))

OBJS := $(MASTER_OBJS) $(WORKER_OBJS)
DEPS := $(OBJS:.o=.d)

all: $(BUILD_DIR)/$(MASTER_TARGET) $(BUILD_DIR)/$(WORKER_TARGET)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/$(MASTER_TARGET): $(MASTER_OBJS)
	$(CC) $(MASTER_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/$(WORKER_TARGET): $(WORKER_OBJS)
	$(CC) $(WORKER_OBJS) -o $@ $(LDFLAGS)


$(BUILD_DIR)/master_%.o: $(MASTER_DIR)/%.c | build
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/worker_%.o: $(WORKER_DIR)/%.c | build
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

# Cleanup
clean:
	rm -rf $(BUILD_DIR)/*

distclean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean distclean build
