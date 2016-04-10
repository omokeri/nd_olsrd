#include "gateway_costs.h"
#include "olsr_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdio.h>

#define SCALING_SHIFT_CLASSIC 31
#define SCALING_SHIFT 23
#define MAX_SMARTGW_SPEED 320000000

int64_t gw_costs_weigh(bool up, uint32_t path_cost, uint32_t exitUk, uint32_t exitDk) {
  int64_t costU;
  int64_t costD;
  int64_t costE;

  if (!up) {
    /* interface is down */
    return INT64_MAX;
  }

  if (!olsr_cnf->smart_gw_divider_etx) {
    /* only consider path costs (classic behaviour) (but scale to 64 bit) */
    return ((int64_t) path_cost) << SCALING_SHIFT_CLASSIC;
  }

  if (!exitUk || !exitDk) {
    /* zero bandwidth */
    return INT64_MAX;
  }

  if ((exitUk >= MAX_SMARTGW_SPEED) //
      && (exitDk >= MAX_SMARTGW_SPEED) //
      && (path_cost < olsr_cnf->smart_gw_path_max_cost_etx_max)) {
    /* maximum bandwidth AND below max_cost_etx_max: only consider path costs */
    return path_cost;
  }

  costU = (((int64_t) (1000      * olsr_cnf->smart_gw_weight_exitlink_up  )) << SCALING_SHIFT) / exitUk;
  costD = (((int64_t) (1000      * olsr_cnf->smart_gw_weight_exitlink_down)) << SCALING_SHIFT) / exitDk;
  costE = (((int64_t) (path_cost * olsr_cnf->smart_gw_weight_etx          )) << SCALING_SHIFT) / olsr_cnf->smart_gw_divider_etx;

  return (costU + costD + costE);
}

double get_gwcost_scaled(int64_t cost) {
  if (cost != INT64_MAX) {
    unsigned int shift = !olsr_cnf->smart_gw_divider_etx ? SCALING_SHIFT_CLASSIC : SCALING_SHIFT;

    double integerNumber = (double) (cost >> shift);
    double fractionNumber = (double) (cost & ((1 << shift) - 1)) / (1 << shift);

    return integerNumber + fractionNumber;
  }

  return (double)cost;
}

const char * get_gwcost_text(int64_t cost, struct gwtextbuffer *buffer) {
  if (cost == INT64_MAX) {
    return "INFINITE";
  }

  snprintf(buffer->buf, sizeof(buffer->buf), "%.3f", get_gwcost_scaled(cost));
  buffer->buf[sizeof(buffer->buf) - 1] = '\0';
  return buffer->buf;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
