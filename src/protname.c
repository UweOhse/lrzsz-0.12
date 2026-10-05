#include "zglobal.h"

const char *
protname(void)
{
	const char *prot_name;
	switch(protocol) {
	case ZM_XMODEM:
		prot_name="XMODEM"; 
		break;
	case ZM_YMODEM:
		prot_name="YMODEM"; 
		break;
	default: 
		prot_name="ZMODEM";
		break;
	}
	return prot_name;
}
