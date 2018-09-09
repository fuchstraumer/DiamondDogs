#pragma once
#ifndef VPSK_HOST_SUBMISSION_HPP
#define VPSK_HOST_SUBMISSION_HPP
#include "ForwardDecl.hpp"
#include "PipelineResource.hpp"
#include <memory>
#include <vector>

namespace vpsk {

    /*
        Whereas a pipeline submission is for gpu-exclusive work that requires a pipeline to be bound, 
        host submissions are for:
        - Clearing and filling buffers
        - Clearing and filling images
        - Satisfying requests to transfer data from disk to the GPU
        - Handling reading/writing data to objects
    */

    class HostSubmission {
    public:
        
    private:

    };

}

#endif // !VPSK_HOST_SUBMISSION_HPP
