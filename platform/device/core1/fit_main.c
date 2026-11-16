#include <stdint.h>

#include "device/core1_control.h"
#include "device/core1_stream.h"
#include "dsp/equalizer_fit.h"
#include "registers.h"

enum {
    WORK_WORDS = 6144
};

void core1_main(void);

static int64_t work[WORK_WORDS];
static Core1FitJob job;

void core1_main(void)
{
    volatile Core1FitJob *shared = (volatile Core1FitJob *)(uintptr_t)CORE1_SHARED_FIT;
    job = *(const Core1FitJob *)(uintptr_t)shared;
    register_write(CORE1_SHARED_READY, CORE1_READY_MAGIC);
    __asm__ volatile("dsb" ::: "memory");
    register_write(CORE1_MAILBOX_B2A_COMMAND, CORE1_READY_COMMAND);
    register_write(CORE1_MAILBOX_B2A_DATA, 0);
    if (register_read(CORE1_SHARED_COMMAND) != CORE1_COMMAND_FIT) {
        return;
    }
    EqualizerFitPart part = {.part = job.part, .parts = job.parts, .beat = 0, .seed = &job.seed};
    EqualizerFitStats stats;
    bool found =
        equalizer_fit_part(&job.curve, job.rate, work, sizeof work, part, &job.fit, &stats);
    *(EqualizerFit *)(uintptr_t)&shared->fit = job.fit;
    shared->found = found ? 1u : 0u;
    __asm__ volatile("dsb" ::: "memory");
    shared->done = 1u;
    for (;;) {
        __asm__ volatile("wfi");
    }
}
