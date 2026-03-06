BCC ?= ../build/bcc
BINDIR ?= ../build/demos

DEMOS = 01_fibonacci 02_primes 03_collatz 04_result_calculator \
        05_temperature 06_generic_pair 07_producer_consumer \
        08_json_addressbook 09_traffic_light 10_math_library

BINS = $(addprefix $(BINDIR)/, $(DEMOS))

.PHONY: all run clean list help

all: $(BINS)

$(BINDIR)/%: %.b $(BCC) | $(BINDIR)
	@echo "  BCC   $<"
	@$(BCC) $< -o $@

$(BINDIR):
	@mkdir -p $@

run: $(BINS)
	@passed=0; failed=0; \
	for demo in $(DEMOS); do \
		echo "--- $$demo ---"; \
		if $(BINDIR)/$$demo; then \
			echo "PASS  $$demo"; echo; \
			passed=$$((passed + 1)); \
		else \
			echo "FAIL  $$demo (exit $$?)"; echo; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "=== Results: $$passed passed, $$failed failed ==="; \
	[ $$failed -eq 0 ]

# Generate per-demo run targets: make run-01_fibonacci, etc.
define RUN_DEMO
.PHONY: run-$(1)
run-$(1): $(BINDIR)/$(1)
	@$(BINDIR)/$(1)
endef
$(foreach d,$(DEMOS),$(eval $(call RUN_DEMO,$(d))))

list:
	@for demo in $(DEMOS); do \
		desc=$$(head -1 $$demo.b | sed 's|^// ||'); \
		printf "  %-25s %s\n" "$$demo" "$$desc"; \
	done

clean:
	rm -rf $(BINDIR)

help:
	@echo "Usage:"
	@echo "  make                Build all demos"
	@echo "  make run            Build and run all demos"
	@echo "  make run-01_fibonacci  Build and run a single demo"
	@echo "  make list           List available demos"
	@echo "  make clean          Remove built binaries"
	@echo ""
	@echo "Override compiler:  make BCC=/path/to/bcc"
