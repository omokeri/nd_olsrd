#include "gateway_costs.h"
#include "olsr_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

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

#ifdef __cplusplus
}
#endif /* __cplusplus */
