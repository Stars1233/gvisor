load("//tools:defs.bzl", "go_library", "go_test")

package(
    default_applicable_licenses = ["//:license"],
    licenses = ["notice"],
)

go_library(
    name = "log",
    srcs = [
        "glog.go",
        "json.go",
        "json_k8s.go",
        "log.go",
        "rate_limited.go",
        "warn_on.go",
    ],
    marshal = False,
    stateify = False,
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//pkg/linewriter",
        "//pkg/sync",
        "@org_golang_x_time//rate:go_default_library",
    ],
)

go_test(
    name = "log_test",
    size = "small",
    srcs = [
        "json_test.go",
        "log_test.go",
        "warn_on_test.go",
    ],
    library = ":log",
)
