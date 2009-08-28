
testfunc () {
	 print(euclid(1071, 462));
}

integer euclid (integer a, integer b) {
	integer t;
	while(b != 0) {
	   t = b; b = a % b; a = t;
        }
	return a;
}

default {
  state_entry(integer foo) {
  }
}
