
--[[
    Example of all potential fields and members for pipeline customization
    Unless otherwise noted, this reflects the default values.
]]
ExampleDefaultPipeline = {
    -- Vertex info depends on attached shader groups and internal vertex layouts
    AssemblyInfo = {
        Topology = "TriangleList",
        PrimitiveRestartEnable = false
    },
    TesselationInfo = {
        --[[
            If left out (default, really) or blank thats ok. 
            TesselationInfo isn't required to create a valid pipeline
        ]]
        PatchControlPoints = 1
    },
    ViewportInfo = {
        ViewportCount = 1,
        Viewports = {
            -- offset x, offset Y, X size, Y Size, Min depth, Max Depth
            { 0, 0, GetWindowX(), GetWindowY(), GetZNear(), GetZFar() }
        },
        ScissorCount = 1,
        Scissors = {
            { 0, 0, GetWindowX(), GetWindowY() }
        }
    },
    RasterizationInfo = {
        DepthClampEnable = false,
        RasterizationDiscardEnable = false,
        -- Options are "Fill", "Line", "Point"
        PolygonMode = "Fill",
        -- Options are "Front" and "Back"
        CullMode = "Back",
        -- Options are CounterClockwise, Clockwise
        FrontFace = "CounterClockwise",
        DepthBiasEnable = false,
        DepthBiasConstantFactor = 0.0,
        DepthBiasClamp = 0.0,
        DepthBiasSlopeFactor = 0.0,
        LineWidth = 1.0
    },
    -- Multisample state information handled by backend/engine graphics config
    DepthStencilInfo = {
        DepthTestEnable = true,
        DepthWriteEnable = true,
        --[[
            Options are:
            Never, Less, Equal,
            LessOrEqual, Greater, NotEqual,
            GreaterOrEqual, Always
        ]]
        DepthCompareOp = "LessOrEqual",
        DepthBoundsTestEnable = false,
        StencilTestEnable = false,
        FrontStencilOpState = {
            --[[
                FailOp, PassOp, DepthFailOp are all the same type
                and have the following options:
                "Keep", "Zero", "Replace", "IncrementAndClamp",
                "DecrementAndClamp", "Invert", "IncrementAndWrap",
                "DecrementAndWrap"
            ]]
            FailOp = "Zero",
            PassOp = "Keep",
            DepthFailOp = "Zero",
            -- Compare op has some options range as "DepthCompareOp"
            CompareOp = "Less",
            -- masks should be specified as ints
            CompareMask = 0xffff,
            WriteMask = 0,
            Reference = 0
        },
        BackStencilOpState = {
            FailOp = "Zero",
            PassOp = "Keep",
            DepthFailOp = "Zero",
            CompareOp = "Less",
            CompareMask = 0xffff,
            WriteMask = 0,
            Reference = 0
        },
        MinDepthBounds = GetZNear(),
        MaxDepthBounds = GetZFar()
    },
    ColorBlendInfo = {
        LogicOpEnable = false,
        --[[
            LogicOp options are:
            "Clear", "And", "AndReverse", "Copy",
            "AndInverted", "NoOp", "Xor", "Or", "Nor",
            "Equivalent", "Invert", "OrReverse", "CopyInverted",
            "OrInverted", "Nand", "Set"
        ]]
        LogicOp = "Copy",
        AttachmentCount = 1,
        Attachments = {
            -- List attachments with indices, as otherwise 
            -- the order will not be preserved
            { 0, {
                BlendEnable = true,
                --[[ Fields with "BlendFactor" in the name have the following
                    options, matching the VkBlendFactor enum:
                    "Zero", "One", "SrcColor", "OneMinusSrcColor", "DstColor",
                    "OneMinusDstColor", "SrcAlpha", "OneMinusSrcAlpha", "DstAlpha",
                    "OneMinusDstAlpha", "ConstantColor", "OneMinusConstantColor",
                    "ConstantAlpha", "OneMinusConstantAlpha", "AlphaSaturate", "Src1Color",
                    "OneMinusSrc1Color", "Src1Alpha", "OneMinusSrc1Alpha"
                ]]
                SrcColorBlendFactor = "SrcColor",
                DstColorBlendFactor = "OneMinusSrcColor"
                --[[
                    Fields with "BlendOp" have the following options, matching the VkBlendOp enum:
                    "Add", "Subtract", "ReverseSubtract", "Min", "Max"
                ]]
                ColorBlendOp = "Add",
                SrcAlphaBlendFactor = "SrcAlpha",
                DstAlphaBlendFactor = "OneMinusSrcAlpha",
                AlphaBlendOp = "Add",
                -- List desired color components piece-by-piece
                ColorWriteMask = "RGBA"
            }}
        }
        -- Four floats detailing constants to use per RGBA component
        BlendConstants = { 1.0, 1.0, 1.0, 1.0 }
    },
    DynamicStateInfo = {
        DynamicStateCount = 2,
        --[[
            Dynamic state options are:
            "Viewport", "Scissor", "LineWidth", "DepthBias",
            "BlendConstants", "DepthBounds", "StencilCompareMask",
            "StencilWriteMask", "StencilReference"
        ]]
        DynamicStates = {
            "Viewport", "Scissor"
        }
    },
    -- Data parsed from this will be combined with ShaderGroup information to fill out
    -- the layout, renderPass, and subpass fields of a VkGraphicsPipelineCreateInfo structure.
    ShaderGroupName = "Default"
    -- Used to set the base pipeline handle or index field, as appropriate,
    -- to enable pipeline derivatives and speed up pipeline construction
    -- Default is actually "None", which just says there's no suitable base pipeline
    -- to use.
    BasePipelineName = "BasePipeline"
}

--[[
    This pipeline shows the bare minimum of information required to create
    a pipeline.
]]
ExampleFromDefaultPipeline = {
    --[[
        All thats really needed is a shader group name. 
        Feasible defaults exist for the rest of the values we can
        modify, and for things like the VertexInfo and ShaderInfos
        these depend on the shader group anyways
    ]]
    ShaderGroupName = "Default",
    BasePipelineName = "None"
}

ExampleOpaquePass = {
    DepthStencilInfo = {
        DepthWriteEnable = false
    }
    RasterizationInfo = {
        CullMode = "Back"
    }, 
    DynamicStateInfo = {
        DynamicStateCount = 2,
        DynamicStates = {
            "Viewport", "Scissor"
        }
    },
    ShaderGroupName = "Default",
    BasePipelineName = "None"
}

--[[
    Note that this uses the previous pipeline as it's base pipeline,
    meaning it'll be created after the above
]]
ExampleTransparentPass = {
    DepthStencilInfo = {
        DepthWriteEnable = false
    },
    RasterizationInfo = {
        CullMode = "None"
    }
    DynamicStateInfo = {
        DynamicStateCount = 2,
        DynamicStates = {
            "Viewport", "Scissor"
        }
    },
    ShaderGroupName = "Default",
    BasePipelineName = "ExampleOpaquePass"
}