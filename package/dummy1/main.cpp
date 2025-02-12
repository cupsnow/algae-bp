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

	log_d("%s\n", aloe_version(NULL, 0));

	for (int i = 0; i < argc; i++) {
//		std::cout << "argv[" << i + 1 << "/" << argc << "]: " << argv[i]
//			  << std::endl;
		log_d("argv[%d/%d]: %s\n", i + 1, argc, argv[i]);
	}
	return 0;
}
