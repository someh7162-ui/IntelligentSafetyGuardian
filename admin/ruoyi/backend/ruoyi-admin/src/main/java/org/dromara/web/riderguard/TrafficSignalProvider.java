package org.dromara.web.riderguard;

/** A signal source returns an observation; route matching remains outside the provider. */
public interface TrafficSignalProvider {
    TrafficSignalService.Signal query(TrafficSignalQuery query);
}
