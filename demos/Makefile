BCC ?= ../build/bcc
BINDIR ?= ../build/demos

DEMOS = 01_fibonacci 02_primes 03_collatz 04_result_calculator \
        05_temperature 06_generic_pair 07_producer_consumer \
        08_json_addressbook 09_traffic_light 10_math_library

# Network demos build but don't auto-run (server is long-running, client needs args)
NET_DEMOS = 11_echo_server 12_tcp_client 13_http_server

BINS = $(addprefix $(BINDIR)/, $(DEMOS) $(NET_DEMOS))

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
$(foreach d,$(NET_DEMOS),$(eval $(call RUN_DEMO,$(d))))

list:
	@for demo in $(DEMOS) $(NET_DEMOS); do \
		desc=$$(head -1 $$demo.b | sed 's|^// ||'); \
		printf "  %-25s %s\n" "$$demo" "$$desc"; \
	done

clean:
	rm -rf $(BINDIR)

help:
	@echo "Usage:"
	@echo "  make                Build all demos"
	@echo "  make run            Build and run all (non-network) demos"
	@echo "  make run-01_fibonacci  Build and run a single demo"
	@echo "  make list           List available demos"
	@echo "  make clean          Remove built binaries"
	@echo ""
	@echo "Network demos (build only, run manually):"
	@echo "  make run-11_echo_server    Start echo server on port 8080"
	@echo "  make run-12_tcp_client     Usage: $(BINDIR)/12_tcp_client <host> <port> <msg>"
	@echo "  make run-13_http_server    Start HTTP server on port 8080"
	@echo ""
	@echo "Override compiler:  make BCC=/path/to/bcc"
