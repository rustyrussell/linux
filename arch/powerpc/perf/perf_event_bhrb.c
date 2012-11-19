#define BHRB_TARGET		0x0000000000000002
#define	BHRB_PREDICTION		0x0000000000000001
#define	BHRB_EA			0xFFFFFFFFFFFFFFFC

extern unsigned long int read_bhrb(int n);

void power_pmu_bhrb_read(struct cpu_hw_events *cpuhw)
{
	u64 val;
	u64 addr;
	int r_index, u_index, target, pred;

	r_index = 0;
	u_index = 0;
	while (r_index < ppmu->bhrb_nr) {
		/* Assembly read function */
		val = read_bhrb(r_index);

		/* Terminal marker: End of valid BHRB entries */
		if (val == 0) {
			break;
		} else {
			/* BHRB field break up */
			addr = val & BHRB_EA;
			pred = val & BHRB_PREDICTION;
			target = val & BHRB_TARGET;

			/* Probable Missed entry: Not applicable for POWER8 */
			if ((addr == 0) && (target == 0) && (pred == 1)) {
				r_index++;
				continue;
			}

			/* Real Missed entry: Power8 based missed entry */
			if ((addr == 0) && (target == 1) && (pred == 1)) {
				r_index++;
				continue;
			}

			/* Reserved condition: Not a valid entry  */
			if ((addr == 0) && (target == 1) && (pred == 0)) {
				r_index++;
				continue;
			}

			/* Is a target address */
			if (val & BHRB_TARGET) {
				/* First address cannot be a target address */
				if (r_index == 0) {
					r_index++;
					continue;
				}

				/* Update target address for the previous entry */
				cpuhw->bhrb_entries[u_index - 1].to = addr;
				cpuhw->bhrb_entries[u_index - 1].mispred = pred;
				cpuhw->bhrb_entries[u_index - 1].predicted = ~pred;

				/* Dont increment u_index */
				r_index++;
			} else {
				/* Update address, flags for current entry */
				cpuhw->bhrb_entries[u_index].from = addr;
				cpuhw->bhrb_entries[u_index].mispred = pred;
				cpuhw->bhrb_entries[u_index].predicted = ~pred;

				/* Successfully popullated one entry */
				u_index++;
				r_index++;
			}
		}
	}
	cpuhw->bhrb_stack.nr = u_index;
	return;
}
