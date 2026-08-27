# expect error: in fn 'inner'
let inner = fn() { undefined_global }
let outer = fn() { inner() }
outer()
