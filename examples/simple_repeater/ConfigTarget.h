#pragma once

// Which config object a "get config"/"put config" CLI command targets.
// CFG_FILTER/CFG_REPORT only exist when the corresponding optional
// engine is compiled in (see ChannelFilter.h / ReportEngine.h, which
// define WITH_CHANNEL_FILTER / WITH_REPORT_ENGINE when included).
enum ConfigTarget {
  CFG_PREFS
  ,
  CFG_FILTER
  ,
  CFG_REPORT
};