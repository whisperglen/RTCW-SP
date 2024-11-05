#include "vm_local.h"

int VM_CallCompiled( vm_t *vm, int *args ) {
	Com_Error(ERR_DROP, "VM_NONE: called VM_CallCompiled");
	return 0;
}

void VM_Compile( vm_t *vm, vmHeader_t *header ) {
	Com_Error(ERR_DROP, "VM_NONE: called VM_Compile");
}
