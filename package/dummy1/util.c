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

#include "priv.h"

const char* algae_version(algae_version_t *ver) {
	static char ver_str[64] = {};
	int r;

	if (!ver_str[0]) {
		if ((r = snprintf(ver_str, sizeof(ver_str), "%s%d.%d.%d %d\n",
				"algae v",
				ALGAE_VER_MAJOR, ALGAE_VER_MINOR, ALGAE_VER_RELEASE,
				ALGAE_VER_CODE)) <= 0
				|| r >= sizeof(ver_str)) {
			log_e("Insufficient buf\n");
			ver_str[0] = '\0';
		}
	}
	if (ver) {
		ver->major = ALGAE_VER_MAJOR;
		ver->minor = ALGAE_VER_MINOR;
		ver->release = ALGAE_VER_RELEASE;
		ver->code = ALGAE_VER_CODE;
	}
	return ver_str;
}
