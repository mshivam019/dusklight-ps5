/* Keep the writable symbol descriptor out of the converter's RELRO area.
 * Otherwise LLD may place this orphan between the GOT and init array, making
 * it overlap the PS5 process-parameter space. Preserve its section name.
 * SPDX-License-Identifier: MIT
 */
SECTIONS {
    symdbh : { KEEP(*(symdbh)) }
} INSERT AFTER .data;
