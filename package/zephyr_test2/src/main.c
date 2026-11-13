#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void) {
	unsigned count = 0;

	while (1) {
		printk("count: %d, uptime: %lld ms\n", count++, 
				(unsigned long long)k_uptime_get());
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
