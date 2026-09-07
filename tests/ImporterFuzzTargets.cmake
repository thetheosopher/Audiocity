include_guard(GLOBAL)

# Keep the first-party importer fuzz matrix authoritative in one place. REX is
# intentionally excluded because its runtime is Windows-only and proprietary.
set(AUDIOCITY_IMPORTER_FUZZ_FORMATS
    sfz
    nki
    sf2
    decent
    bitwig
    mpc
    bento
    talsmpl
    tx16wx
    korgmulti
    ableton
    distingex
    kmp
    exs24
    nnxt)

function(audiocity_prepare_importer_fuzz_corpus format output_directory output_inputs)
    if (NOT "${format}" IN_LIST AUDIOCITY_IMPORTER_FUZZ_FORMATS)
        message(FATAL_ERROR "Unknown Audiocity importer fuzz format: ${format}")
    endif ()

    set(source_directory "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/fuzz/corpus/${format}")
    file(GLOB seed_inputs CONFIGURE_DEPENDS "${source_directory}/*")
    if (NOT seed_inputs)
        message(FATAL_ERROR "Importer fuzz corpus is empty: ${source_directory}")
    endif ()

    # libFuzzer may add minimized/generated inputs to its first corpus path, so
    # always give it a disposable build-tree copy rather than the source tree.
    set(binary_directory "${CMAKE_CURRENT_BINARY_DIR}/fuzz-corpus/${format}")
    file(REMOVE_RECURSE "${binary_directory}")
    file(MAKE_DIRECTORY "${binary_directory}")
    file(COPY ${seed_inputs} DESTINATION "${binary_directory}")

    set(binary_inputs)
    foreach (seed_input IN LISTS seed_inputs)
        get_filename_component(seed_name "${seed_input}" NAME)
        list(APPEND binary_inputs "${binary_directory}/${seed_name}")
    endforeach ()

    set(${output_directory} "${binary_directory}" PARENT_SCOPE)
    set(${output_inputs} "${binary_inputs}" PARENT_SCOPE)
endfunction()
