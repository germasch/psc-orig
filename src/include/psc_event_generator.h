
#include <mrc_obj.h>

#include "psc.h"

MRC_CLASS_DECLARE(psc_event_generator, struct psc_event_generator);

void psc_event_generator_run(struct psc_event_generator *gen,
			     mparticles_base_t *mparticles, mfields_base_t *mflds,
			     mphotons_t *mphotons);
