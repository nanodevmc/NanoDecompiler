#pragma once

#include <map>
#include <string>
#include <vector>
#include "classfile.hpp"

namespace nd {

struct MethodSemanticContext {
    std::string method_name;
    std::string suggested_name;
    std::string javadoc;
    std::map<int, std::string> parameter_names;
};

struct ClassSemanticContext {
    std::string class_name;
    std::string javadoc;
    std::vector<MethodSemanticContext> methods;
};

class SemanticReconstructor {
public:
    static ClassSemanticContext analyze_class(const ClassFile& cf);
    static std::string suggest_parameter_name(const std::string& type_desc, int param_index);
    static std::string generate_method_javadoc(const Method& m, const ClassFile& cf);
    static std::string generate_class_javadoc(const ClassFile& cf);
};

}  // namespace nd
