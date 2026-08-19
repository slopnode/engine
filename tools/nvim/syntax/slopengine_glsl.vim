if exists("b:current_syntax")
  finish
endif

syntax case match

syntax match slopengineGlslComment "//.*$"
syntax region slopengineGlslComment start="/\*" end="\*/"

syntax match slopengineGlslPreprocessor "^\s*#\s*\(version\|define\|undef\|include\|if\|ifdef\|ifndef\|else\|elif\|endif\|pragma\|extension\|line\|error\)\>.*$"

syntax region slopengineGlslString start=/"/ skip=/\\./ end=/"/ contains=slopengineGlslEscape
syntax match slopengineGlslEscape /\\./ contained

syntax match slopengineGlslNumber "\v<(0[xX][0-9a-fA-F]+|\d+\.\d+([eE][+-]?\d+)?[fF]?|\.\d+[fF]?|\d+\.[fF]?|\d+[eE][+-]?\d+[fF]?|\d+[uU]?[fF]?)>"

syntax keyword slopengineGlslStorageClass in out inout uniform attribute varying const layout flat smooth noperspective centroid patch sample buffer shared readonly writeonly coherent volatile restrict precision highp mediump lowp invariant

syntax keyword slopengineGlslConditional if else switch case default
syntax keyword slopengineGlslRepeat for while do
syntax keyword slopengineGlslStatement return break continue discard

syntax keyword slopengineGlslType void bool int uint float double struct
syntax keyword slopengineGlslType vec2 vec3 vec4 ivec2 ivec3 ivec4 uvec2 uvec3 uvec4 bvec2 bvec3 bvec4 dvec2 dvec3 dvec4
syntax keyword slopengineGlslType mat2 mat3 mat4 mat2x2 mat2x3 mat2x4 mat3x2 mat3x3 mat3x4 mat4x2 mat4x3 mat4x4
syntax keyword slopengineGlslType sampler1D sampler2D sampler3D samplerCube sampler2DArray sampler2DShadow samplerCubeShadow sampler2DArrayShadow isampler2D usampler2D image1D image2D image3D imageCube

syntax keyword slopengineGlslBuiltinVar gl_Position gl_FragCoord gl_FragColor gl_FragDepth gl_PointSize gl_PointCoord gl_VertexID gl_InstanceID gl_FrontFacing gl_GlobalInvocationID gl_LocalInvocationID gl_WorkGroupID gl_NumWorkGroups

syntax match slopengineGlslBuiltinFunc "\<\(texture\|texture2D\|textureCube\|textureLod\|texelFetch\|textureSize\|normalize\|dot\|cross\|mix\|clamp\|min\|max\|pow\|sqrt\|inversesqrt\|length\|distance\|reflect\|refract\|floor\|ceil\|fract\|mod\|abs\|sign\|sin\|cos\|tan\|asin\|acos\|atan\|exp\|exp2\|log\|log2\|smoothstep\|step\|inverse\|transpose\|determinant\|matrixCompMult\|dFdx\|dFdy\|fwidth\|round\|trunc\)\>\(\s*(\)\@="

syntax match slopengineGlslFunctionCall "\<[a-zA-Z_][a-zA-Z0-9_]*\>\(\s*(\)\@="

highlight default link slopengineGlslComment Comment
highlight default link slopengineGlslPreprocessor PreProc
highlight default link slopengineGlslString String
highlight default link slopengineGlslEscape SpecialChar
highlight default link slopengineGlslNumber Number
highlight default link slopengineGlslStorageClass StorageClass
highlight default link slopengineGlslConditional Conditional
highlight default link slopengineGlslRepeat Repeat
highlight default link slopengineGlslStatement Statement
highlight default link slopengineGlslType Type
highlight default link slopengineGlslBuiltinVar Identifier
highlight default link slopengineGlslBuiltinFunc Function
highlight default link slopengineGlslFunctionCall Function

let b:current_syntax = "slopengine_glsl"
