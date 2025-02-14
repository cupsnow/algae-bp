/* $Id$
 *
 * Copyright 2023, Dexatek Technology Ltd.
 * This is proprietary information of Dexatek Technology Ltd.
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of Dexatek Technology Ltd. is strictly forbidden.
 *
 * @author joelai
 */

#include <iostream>
#include "priv.h"

int main(int argc, const char **argv) {
	char *mm;

	log_d("%s\n", aloe_version(NULL, 0));

	mm = (char*)aloe_calloc(3, 2);
	if (aloe_free(mm) != 0) {
		log_e("Sanity check unexpected aloe_free\n");
	}

	mm = (char*)aloe_calloc(3, 2);
	mm[6] = 'c';
	log_d("expect report overflow\n");
	if (aloe_free(mm) == 0) {
		log_e("Sanity check unexpected aloe_free\n");
	}

	for (int i = 0; i < argc; i++) {
//		std::cout << "argv[" << i + 1 << "/" << argc << "]: " << argv[i]
//			  << std::endl;
		log_d("argv[%d/%d]: %s\n", i + 1, argc, argv[i]);
	}
	return 0;
}
