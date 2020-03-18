#include "thor/pathalgorithm.h"
#include "baldr/datetime.h"
#include "midgard/logging.h"
#include "sif/edgelabel.h"

namespace dt = valhalla::baldr::DateTime;

namespace valhalla {
namespace thor {

// helper function to initialize the object from a location
TimeInfo TimeInfo::make(valhalla::Location& location, baldr::GraphReader& reader) {
  // no time to to track
  if (!location.has_date_time()) {
    return {false};
  }

  // get the timezone of the first edge's end node that we can
  int timezone_index = 0;
  for (const auto& pe : location.path_edges()) {
    const baldr::GraphTile* tile = nullptr;
    const auto* edge = reader.directededge(baldr::GraphId(pe.graph_id()), tile);
    timezone_index = reader.GetTimezone(edge->endnode(), tile);
    if (timezone_index != 0)
      break;
  }

  // Set the origin timezone to be the timezone at the end node
  if (timezone_index == 0) {
    LOG_WARN("Could not get the timezone at the origin");
  }

  // Set the time for the current time route
  bool current = false;
  if (location.date_time() == "current") {
    current = true;
    location.set_date_time(dt::iso_date_time(dt::get_tz_db().from_index(timezone_index)));
  }

  // Set route start time (seconds from epoch)
  auto local_time =
      dt::seconds_since_epoch(location.date_time(), dt::get_tz_db().from_index(timezone_index));

  // Set seconds from beginning of the week
  auto second_of_week = dt::day_of_week(location.date_time()) * valhalla::midgard::kSecondsPerDay +
                        dt::seconds_from_midnight(location.date_time());

  // construct the info
  return {true, timezone_index, local_time, second_of_week, current};
}

// offset all the initial time info to reflect the progress along the route to this point
TimeInfo TimeInfo::operator+(Offset offset) const {
  if (!valid)
    return *this;

  // if the timezone changed we need to account for that offset as well
  int tz_diff = 0;
  if (offset.timezone_index != timezone_index) {
    tz_diff =
        dt::timezone_diff(local_time + offset.seconds, dt::get_tz_db().from_index(timezone_index),
                          dt::get_tz_db().from_index(offset.timezone_index));
  }

  // offset the local time and second of week by the amount traveled to this label
  uint64_t lt = local_time + static_cast<uint64_t>(offset.seconds) + tz_diff;
  uint32_t sw = second_of_week + static_cast<uint32_t>(offset.seconds) + tz_diff;
  if (sw > valhalla::midgard::kSecondsPerWeek) {
    sw -= valhalla::midgard::kSecondsPerWeek;
  }

  // return the shifted object, notice that seconds from now is only useful for
  // date_time type == current
  return {valid, offset.timezone_index, lt, sw, current, offset.seconds};
}

// offset all the initial time info to reflect the progress along the route to this point
TimeInfo TimeInfo::operator-(Offset offset) const {
  if (!valid)
    return *this;

  // if the timezone changed we need to account for that offset as well
  int tz_diff = 0;
  if (offset.timezone_index != timezone_index) {
    tz_diff =
        dt::timezone_diff(local_time + offset.seconds, dt::get_tz_db().from_index(timezone_index),
                          dt::get_tz_db().from_index(offset.timezone_index));
  }

  // offset the local time and second of week by the amount traveled to this label
  uint64_t lt = local_time - static_cast<uint64_t>(offset.seconds) + tz_diff; // dont route in 1970
  int64_t sw = static_cast<int64_t>(second_of_week) - static_cast<int64_t>(offset.seconds) + tz_diff;
  if (sw < 0) {
    sw = valhalla::midgard::kSecondsPerWeek + sw;
  }

  // return the shifted object, notice that seconds from now is negative, this could be useful if
  // we had the ability to arrive_by current time but we dont for the moment
  return {valid, offset.timezone_index, lt, static_cast<uint32_t>(sw), current, -offset.seconds};
}

TimeInfo::operator bool() const {
  return valid;
}

} // namespace thor
} // namespace valhalla