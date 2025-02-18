//
// Copyright (c) 2016 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// DeferGlobalInitializers is an AST traverser that moves global initializers into a function, and
// adds a function call to that function in the beginning of main().
// This enables initialization of globals with uniforms or non-constant globals, as allowed by
// the WebGL spec. Some initializers referencing non-constants may need to be unfolded into if
// statements in HLSL - this kind of steps should be done after DeferGlobalInitializers is run.
//

#include "compiler/translator/DeferGlobalInitializers.h"

#include "compiler/translator/IntermNode.h"
#include "compiler/translator/SymbolTable.h"

namespace
{

void SetInternalFunctionName(TIntermAggregate *functionNode, const char *name)
{
    TString nameStr(name);
    nameStr = TFunction::mangleName(nameStr);
    TName nameObj(nameStr);
    nameObj.setInternal(true);
    functionNode->setNameObj(nameObj);
}

TIntermAggregate *CreateFunctionPrototypeNode(const char *name, const int functionId)
{
    TIntermAggregate *functionNode = new TIntermAggregate(EOpPrototype);

    SetInternalFunctionName(functionNode, name);
    TType returnType(EbtVoid);
    functionNode->setType(returnType);
    functionNode->setFunctionId(functionId);
    return functionNode;
}

TIntermAggregate *CreateFunctionDefinitionNode(const char *name,
                                               TIntermAggregate *functionBody,
                                               const int functionId)
{
    TIntermAggregate *functionNode = new TIntermAggregate(EOpFunction);
    TIntermAggregate *paramsNode = new TIntermAggregate(EOpParameters);
    functionNode->getSequence()->push_back(paramsNode);
    functionNode->getSequence()->push_back(functionBody);

    SetInternalFunctionName(functionNode, name);
    TType returnType(EbtVoid);
    functionNode->setType(returnType);
    functionNode->setFunctionId(functionId);
    return functionNode;
}

TIntermAggregate *CreateFunctionCallNode(const char *name, const int functionId)
{
    TIntermAggregate *functionNode = new TIntermAggregate(EOpFunctionCall);

    functionNode->setUserDefined();
    SetInternalFunctionName(functionNode, name);
    TType returnType(EbtVoid);
    functionNode->setType(returnType);
    functionNode->setFunctionId(functionId);
    return functionNode;
}

class DeferGlobalInitializersTraverser : public TIntermTraverser
{
  public:
    DeferGlobalInitializersTraverser();

    bool visitBinary(Visit visit, TIntermBinary *node) override;

    void insertInitFunction(TIntermNode *root);

  private:
    TIntermSequence mDeferredInitializers;
};

DeferGlobalInitializersTraverser::DeferGlobalInitializersTraverser()
    : TIntermTraverser(true, false, false)
{
}

bool DeferGlobalInitializersTraverser::visitBinary(Visit visit, TIntermBinary *node)
{
    if (node->getOp() == EOpInitialize)
    {
        TIntermSymbol *symbolNode = node->getLeft()->getAsSymbolNode();
        ASSERT(symbolNode);
        TIntermTyped *expression = node->getRight();

        if (mInGlobalScope && (expression->getQualifier() != EvqConst ||
                               (expression->getAsConstantUnion() == nullptr &&
                                !expression->isConstructorWithOnlyConstantUnionParameters())))
        {
            // For variables which are not constant, defer their real initialization until
            // after we initialize uniforms.
            // Deferral is done also in any cases where the variable has not been constant folded,
            // since otherwise there's a chance that HLSL output will generate extra statements
            // from the initializer expression.
            TIntermBinary *deferredInit = new TIntermBinary(EOpAssign);
            deferredInit->setLeft(symbolNode->deepCopy());
            deferredInit->setRight(node->getRight());
            deferredInit->setType(node->getType());
            mDeferredInitializers.push_back(deferredInit);

            // Change const global to a regular global if its initialization is deferred.
            // This can happen if ANGLE has not been able to fold the constant expression used
            // as an initializer.
            ASSERT(symbolNode->getQualifier() == EvqConst ||
                   symbolNode->getQualifier() == EvqGlobal);
            if (symbolNode->getQualifier() == EvqConst)
            {
                // All of the siblings in the same declaration need to have consistent qualifiers.
                auto *siblings = getParentNode()->getAsAggregate()->getSequence();
                for (TIntermNode *siblingNode : *siblings)
                {
                    TIntermBinary *siblingBinary = siblingNode->getAsBinaryNode();
                    if (siblingBinary)
                    {
                        ASSERT(siblingBinary->getOp() == EOpInitialize);
                        siblingBinary->getLeft()->getTypePointer()->setQualifier(EvqGlobal);
                    }
                    siblingNode->getAsTyped()->getTypePointer()->setQualifier(EvqGlobal);
                }
                // This node is one of the siblings.
                ASSERT(symbolNode->getQualifier() == EvqGlobal);
            }
            // Remove the initializer from the global scope and just declare the global instead.
            mReplacements.push_back(NodeUpdateEntry(getParentNode(), node, symbolNode, false));
        }
    }
    return false;
}

void DeferGlobalInitializersTraverser::insertInitFunction(TIntermNode *root)
{
    if (mDeferredInitializers.empty())
    {
        return;
    }
    const int initFunctionId  = TSymbolTable::nextUniqueId();
    TIntermAggregate *rootAgg = root->getAsAggregate();
    ASSERT(rootAgg != nullptr && rootAgg->getOp() == EOpSequence);

    const char *functionName = "initializeDeferredGlobals";

    // Add function prototype to the beginning of the shader
    TIntermAggregate *functionPrototypeNode =
        CreateFunctionPrototypeNode(functionName, initFunctionId);
    rootAgg->getSequence()->insert(rootAgg->getSequence()->begin(), functionPrototypeNode);

    // Add function definition to the end of the shader
    TIntermAggregate *functionBodyNode = new TIntermAggregate(EOpSequence);
    TIntermSequence *functionBody = functionBodyNode->getSequence();
    for (const auto &deferredInit : mDeferredInitializers)
    {
        functionBody->push_back(deferredInit);
    }
    TIntermAggregate *functionDefinition =
        CreateFunctionDefinitionNode(functionName, functionBodyNode, initFunctionId);
    rootAgg->getSequence()->push_back(functionDefinition);

    // Insert call into main function
    for (TIntermNode *node : *rootAgg->getSequence())
    {
        TIntermAggregate *nodeAgg = node->getAsAggregate();
        if (nodeAgg != nullptr && nodeAgg->getOp() == EOpFunction &&
            TFunction::unmangleName(nodeAgg->getName()) == "main")
        {
            TIntermAggregate *functionCallNode =
                CreateFunctionCallNode(functionName, initFunctionId);

            TIntermNode *mainBody         = nodeAgg->getSequence()->back();
            TIntermAggregate *mainBodyAgg = mainBody->getAsAggregate();
            ASSERT(mainBodyAgg != nullptr && mainBodyAgg->getOp() == EOpSequence);
            mainBodyAgg->getSequence()->insert(mainBodyAgg->getSequence()->begin(),
                                               functionCallNode);
        }
    }
}

}  // namespace

void DeferGlobalInitializers(TIntermNode *root)
{
    DeferGlobalInitializersTraverser traverser;
    root->traverse(&traverser);

    // Replace the initializers of the global variables.
    traverser.updateTree();

    // Add the function with initialization and the call to that.
    traverser.insertInitFunction(root);
}
