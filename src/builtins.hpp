#pragma once

namespace rill {

class VM;

// Binds every native function into the VM's globals.
void defineBuiltins(VM& vm);

}  // namespace rill
