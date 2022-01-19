/*==============================================================================

  Program: 3D Slicer

  Copyright (c) Kitware Inc.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

// MRMLDisplayableManager includes
#include "vtkMRMLColorLegendDisplayableManager.h"

// MRML includes
#include <vtkMRMLApplicationLogic.h>
#include <vtkMRMLColorLegendDisplayNode.h>
#include <vtkMRMLColorNode.h>
#include <vtkMRMLDisplayableNode.h>
#include <vtkMRMLScalarVolumeDisplayNode.h>
#include <vtkMRMLScene.h>
#include <vtkMRMLSliceCompositeNode.h>
#include <vtkMRMLSliceLogic.h>
#include <vtkMRMLSliceNode.h>
#include <vtkMRMLViewNode.h>
#include <vtkMRMLVolumeNode.h>

// VTK includes
#include <vtkLookupTable.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkSmartPointer.h>
#include <vtkTextProperty.h>

// Slicer includes
#include <vtkSlicerScalarBarActor.h>

namespace
{
const int RENDERER_LAYER = 1; // layer ID where the legent will be displayed
}

//---------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLColorLegendDisplayableManager);

//---------------------------------------------------------------------------
class vtkMRMLColorLegendDisplayableManager::vtkInternal
{
public:

  vtkInternal(vtkMRMLColorLegendDisplayableManager * external);
  virtual ~vtkInternal();

  vtkObserverManager* GetMRMLNodesObserverManager();
  void Modified();

  vtkMRMLSliceCompositeNode* FindSliceCompositeNode();
  bool IsVolumeVisibleInSliceView(vtkMRMLVolumeNode* volumeNode);

  // Update color legend
  void UpdateColorLegend();

  // Update actor and widget representation.
  // Returns true if the actor has changed.
  bool UpdateActor(vtkMRMLColorLegendDisplayNode* dispNode);

  // Show/hide the actor by adding to the renderr and enabling visibility; or removing from the renderer.
  // Returns tru if visibility changed.
  bool ShowActor(vtkSlicerScalarBarActor* actor, bool show);

  void UpdateSliceNode();
  void SetSliceCompositeNode(vtkMRMLSliceCompositeNode* compositeNode);
  void UpdateActorsVisibilityFromSliceCompositeNode();

  vtkMRMLColorLegendDisplayableManager* External;

  /// Map stores color legend display node ID as a key, ScalarBarActor as a value
  std::map< std::string, vtkSmartPointer<vtkSlicerScalarBarActor> > ColorLegendActorsMap;

  /// For volume nodes we need to observe the slice composite node so that we can show color legend
  /// only for nodes that are visible in the slice view.
  vtkWeakPointer<vtkMRMLSliceCompositeNode> SliceCompositeNode;

  vtkSmartPointer<vtkRenderer> ColorLegendRenderer;
};


//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkMRMLColorLegendDisplayableManager::vtkInternal::vtkInternal(vtkMRMLColorLegendDisplayableManager* external)
: External(external)
{
  this->ColorLegendRenderer = vtkSmartPointer<vtkRenderer>::New();
}

//---------------------------------------------------------------------------
vtkMRMLColorLegendDisplayableManager::vtkInternal::~vtkInternal()
{
  this->ColorLegendActorsMap.clear();
}

//---------------------------------------------------------------------------
vtkObserverManager* vtkMRMLColorLegendDisplayableManager::vtkInternal::GetMRMLNodesObserverManager()
{
  return this->External->GetMRMLNodesObserverManager();
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::vtkInternal::Modified()
{
  return this->External->Modified();
}

//---------------------------------------------------------------------------
bool vtkMRMLColorLegendDisplayableManager::vtkInternal::ShowActor(vtkSlicerScalarBarActor* actor, bool show)
{
  if (!this->ColorLegendRenderer.GetPointer())
    {
    return false;
    }
  bool wasInRenderer = this->ColorLegendRenderer->HasViewProp(actor);
  bool wasVisible = wasInRenderer && actor->GetVisibility();
  if (show && !wasInRenderer)
    {
    this->ColorLegendRenderer->AddActor2D(actor);
    }
  else if (!show && wasInRenderer)
    {
    this->ColorLegendRenderer->RemoveActor(actor);
    }
  actor->SetVisibility(show);
  return (wasVisible != show);
}

//---------------------------------------------------------------------------
bool vtkMRMLColorLegendDisplayableManager::vtkInternal::IsVolumeVisibleInSliceView(
  vtkMRMLVolumeNode* volumeNode)
{
  if (!volumeNode)
    {
    return false;
    }
  if (!this->SliceCompositeNode.GetPointer())
    {
    return false;
    }
  const char* volumeNodeID = volumeNode->GetID();
  if (!volumeNodeID)
    {
    return false;
    }
  if (this->SliceCompositeNode->GetBackgroundVolumeID())
    {
    if (strcmp(this->SliceCompositeNode->GetBackgroundVolumeID(), volumeNodeID) == 0)
      {
      return true;
      }
    }
  if (this->SliceCompositeNode->GetForegroundVolumeID())
    {
    if (strcmp(this->SliceCompositeNode->GetForegroundVolumeID(), volumeNodeID) == 0)
      {
      return true;
      }
    }
  if (this->SliceCompositeNode->GetLabelVolumeID())
    {
    if (strcmp(this->SliceCompositeNode->GetLabelVolumeID(), volumeNodeID) == 0)
      {
      return true;
      }
    }
  return false;
}

//---------------------------------------------------------------------------
bool vtkMRMLColorLegendDisplayableManager::vtkInternal::UpdateActor(vtkMRMLColorLegendDisplayNode* colorLegendDisplayNode)
{
  vtkSlicerScalarBarActor* actor = this->External->GetColorLegendActor(colorLegendDisplayNode);
  if (!actor || !colorLegendDisplayNode)
    {
    return false;
    }

  if (!colorLegendDisplayNode->GetVisibility())
    {
    return this->ShowActor(actor, false);
    }
  vtkMRMLDisplayableNode* displayableNode = colorLegendDisplayNode->GetDisplayableNode();
  if (!displayableNode)
    {
    return this->ShowActor(actor, false);
    }

  // Get primary display node
  vtkMRMLDisplayNode* primaryDisplayNode = colorLegendDisplayNode->GetPrimaryDisplayNode();
  if (!primaryDisplayNode && displayableNode)
    {
    // Primary display node is not set, fall back to the first non-color-legend display node of the displayable node
    for (int i = 0; i < displayableNode->GetNumberOfDisplayNodes(); i++)
      {
      if (!vtkMRMLColorLegendDisplayNode::SafeDownCast(displayableNode->GetDisplayNode()))
        {
        // found a suitable (non-color-legend) display node
        primaryDisplayNode = displayableNode->GetDisplayNode();
        break;
        }
      }
    }
  if (!primaryDisplayNode)
    {
    vtkErrorWithObjectMacro(this->External, "UpdateActor failed: No primary display node found");
    return this->ShowActor(actor, false);
    }

  // Setup/update color legend actor visibility
  // Color legend is only visible if the primary display node is visible as well, to reduce clutter in the views.
  vtkMRMLNode* viewNode = this->External->GetMRMLDisplayableNode();
  if (!viewNode)
    {
    return this->ShowActor(actor, false);
    }
  bool visible = colorLegendDisplayNode->GetVisibility(viewNode->GetID());

  if (visible)
    {
    // Only show the color legend if the primary display node is visible in the view, too.
    vtkMRMLVolumeDisplayNode* volumeDisplayNode = vtkMRMLVolumeDisplayNode::SafeDownCast(primaryDisplayNode);
    if (volumeDisplayNode)
      {
      // Volumes are special case, their visibility can be determined from slice view logics
      if (this->SliceCompositeNode)
        {
        // 2D view
        visible &= this->IsVolumeVisibleInSliceView(vtkMRMLVolumeNode::SafeDownCast(volumeDisplayNode->GetDisplayableNode()));
        }
      else
        {
        // 3D view
        // For now don't show color legends for volumes in 3D views.
        // In the future, color legend could be shown for volumes that are shown in slice views
        // that are visible in the 3D view.
        visible = false;
        }
      }
    else
      {
      // For all other nodes (models, markups, ...) visibilitly is determined from the display node.
      visible &= primaryDisplayNode->GetVisibility(viewNode->GetID());
      }
    }

  if (!visible)
    {
    return this->ShowActor(actor, false);
    }

  std::string title = colorLegendDisplayNode->GetTitleText();
  actor->SetTitle(title.c_str());

  actor->SetTitleTextProperty(colorLegendDisplayNode->GetTitleTextProperty());
  actor->SetLabelTextProperty(colorLegendDisplayNode->GetLabelTextProperty());

  std::string format = colorLegendDisplayNode->GetLabelFormat();
  actor->SetLabelFormat(format.c_str());

  double size[2] = { 0.5, 0.5 };
  colorLegendDisplayNode->GetSize(size);

  double position[3] = { 0.0, 0.0, 0.0 };
  colorLegendDisplayNode->GetPosition(position);

  // Set text position to the inner side of the legend
  // (using SetTextPositionTo...ScalarBar)
  // because the text overlapping with the image is typically
  // occludes less of the view contents.

  switch (colorLegendDisplayNode->GetOrientation())
    {
    case vtkMRMLColorLegendDisplayNode::Vertical:
      actor->SetOrientationToVertical();
      actor->SetPosition(position[0] * (1 - size[0]), position[1] * (1 - size[1]));
      actor->SetWidth(size[0]);
      actor->SetHeight(size[1]);
      if (position[0] < 0.5)
        {
        actor->SetTextPositionToSucceedScalarBar();
        actor->SetTextPad(2); // make some space between the bar and labels
        actor->GetTitleTextProperty()->SetJustificationToLeft();
        }
      else
        {
        actor->SetTextPositionToPrecedeScalarBar();
        actor->SetTextPad(-2); // make some space between the bar and labels
        actor->GetTitleTextProperty()->SetJustificationToRight();
        }
      break;
    case vtkMRMLColorLegendDisplayNode::Horizontal:
      actor->SetOrientationToHorizontal();
      actor->SetPosition(position[0] * (1 - size[1]), position[1] * (1 - size[0]));
      actor->SetWidth(size[1]);
      actor->SetHeight(size[0]);
      actor->SetTextPad(0);
      actor->GetTitleTextProperty()->SetJustificationToCentered();
      if (position[1] < 0.5)
        {
        actor->SetTextPositionToSucceedScalarBar();
        }
      else
        {
        actor->SetTextPositionToPrecedeScalarBar();
        }
      break;
    default:
      vtkErrorWithObjectMacro(this->External, "UpdateActor failed to set orientation: unknown orientation type " << colorLegendDisplayNode->GetOrientation());
      break;
    }

  // Get color node from the primary display node.
  // This is what determines the appearance of the displayable node, therefore it must be used
  // and not the color node and range that is set in the colorLegendDisplayNode.
  vtkMRMLColorNode* colorNode = primaryDisplayNode->GetColorNode();
  if (!colorNode)
    {
    vtkErrorWithObjectMacro(this->External, "UpdateActor failed: No color node is set in primary display node");
    return this->ShowActor(actor, false);
    }

  // Update displayed scalars range from primary display node
  double range[2] = { -1.0, -1.0 };
  vtkMRMLScalarVolumeDisplayNode* scalarVolumeDisplayNode = vtkMRMLScalarVolumeDisplayNode::SafeDownCast(primaryDisplayNode);
  if (scalarVolumeDisplayNode)
    {
    // Scalar volume display node
    double window = scalarVolumeDisplayNode->GetWindow();
    double level = scalarVolumeDisplayNode->GetLevel();
    range[0] = level - window / 2.0;
    range[1] = level + window / 2.0;
    }
  else
    {
    // Model or other display node
    primaryDisplayNode->GetScalarRange(range);
    }

  if (primaryDisplayNode->GetScalarRangeFlag() == vtkMRMLDisplayNode::UseDirectMapping)
    {
    // direct RGB color mapping, no LUT is used
    return this->ShowActor(actor, false);
    }

  if (!colorNode->GetLookupTable())
    {
    vtkErrorWithObjectMacro(this->External, "UpdateActor failed: No color node is set in primary display node");
    return this->ShowActor(actor, false);
    }

  // The look up table range, linear/log scale, etc. may need
  // to be changed to render the correct scalar values, thus
  // one lookup table can not be shared by multiple mappers
  // if any of those mappers needs to map using its scalar
  // values range. It is therefore necessary to make a copy
  // of the colorNode vtkLookupTable in order not to impact
  // that lookup table original range.
  vtkSmartPointer<vtkLookupTable> lut = vtkSmartPointer<vtkLookupTable>::Take(colorNode->CreateLookupTableCopy());

  lut->SetTableRange(range);

  if (colorLegendDisplayNode->GetUseColorNamesForLabels() && colorNode->GetNumberOfColors() > 0)
    {
    // When there are only a few colors (e.g., 5-10) in the LUT then it is important to build the
    // color table more color indices, otherwise centered labels would not be show up at the correct
    // position. We oversample the LUT to have approximately 256 color indices (newNumberOfColors)
    // regardless of how many items were in the original color table.
    actor->SetLookupTable(lut);
    double oversampling = 256.0 / colorNode->GetNumberOfColors();
    int newNumberOfColors = colorNode->GetNumberOfColors() * oversampling;
    actor->SetNumberOfLabels(colorNode->GetNumberOfColors());
    actor->SetMaximumNumberOfColors(newNumberOfColors);
    actor->GetLookupTable()->ResetAnnotations();
    for (int colorIndex = 0; colorIndex < newNumberOfColors; ++colorIndex)
      {
      actor->GetLookupTable()->SetAnnotation(colorIndex, vtkStdString(colorNode->GetColorName(colorIndex/oversampling)));
      }
    actor->SetUseAnnotationAsLabel(true);
    actor->SetCenterLabel(true);
    }
  else
    {
    actor->SetNumberOfLabels(colorLegendDisplayNode->GetNumberOfLabels());
    actor->SetMaximumNumberOfColors(colorLegendDisplayNode->GetMaxNumberOfColors());
    actor->SetUseAnnotationAsLabel(false);
    actor->SetCenterLabel(false);
    actor->SetLookupTable(lut);

    }

  this->ShowActor(actor, true);

  // modified
  return true;
}

//---------------------------------------------------------------------------
vtkMRMLSliceCompositeNode* vtkMRMLColorLegendDisplayableManager::vtkInternal::FindSliceCompositeNode()
{
  vtkMRMLNode* viewNode = this->External->GetMRMLDisplayableNode();
  vtkMRMLSliceNode* sliceNode = vtkMRMLSliceNode::SafeDownCast(viewNode);
  if (!sliceNode)
    {
    // this displayable manager is not of a slice node
    return nullptr;
    }
  vtkMRMLApplicationLogic* mrmlAppLogic = this->External->GetMRMLApplicationLogic();
  if (!mrmlAppLogic)
    {
    vtkGenericWarningMacro("vtkMRMLColorLegendDisplayableManager::vtkInternal::FindSliceCompositeNode failed: invalid mrmlApplogic");
    return nullptr;
    }
  vtkMRMLSliceLogic* sliceLogic = mrmlAppLogic->GetSliceLogic(sliceNode);
  if (!sliceLogic)
    {
    return nullptr;
    }
  vtkMRMLSliceCompositeNode* sliceCompositeNode = sliceLogic->GetSliceCompositeNode();
  return sliceCompositeNode;
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::vtkInternal::UpdateSliceNode()
{
  vtkMRMLSliceCompositeNode* sliceCompositeNode = this->FindSliceCompositeNode();
  this->SetSliceCompositeNode(sliceCompositeNode);
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::vtkInternal::SetSliceCompositeNode(vtkMRMLSliceCompositeNode* compositeNode)
{
  if (this->SliceCompositeNode == compositeNode)
    {
    return;
    }
  vtkSetAndObserveMRMLNodeMacro(this->SliceCompositeNode, compositeNode);
  this->External->SetUpdateFromMRMLRequested(true);
  this->External->RequestRender();
}

//---------------------------------------------------------------------------
// vtkMRMLColorLegendDisplayableManager methods

//---------------------------------------------------------------------------
vtkMRMLColorLegendDisplayableManager::vtkMRMLColorLegendDisplayableManager()
{
  this->Internal = new vtkInternal(this);
}

//---------------------------------------------------------------------------
vtkMRMLColorLegendDisplayableManager::~vtkMRMLColorLegendDisplayableManager()
{
  delete this->Internal;
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//---------------------------------------------------------------------------
vtkSlicerScalarBarActor* vtkMRMLColorLegendDisplayableManager::GetColorLegendActor(vtkMRMLColorLegendDisplayNode* dispNode) const
{
  if (!dispNode)
    {
    vtkErrorMacro("GetColorLegendActor: display node is invalid");
    return nullptr;
    }
  const auto it = this->Internal->ColorLegendActorsMap.find(dispNode->GetID());
  if (it == this->Internal->ColorLegendActorsMap.end())
    {
    return nullptr;
    }
  return it->second;
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::Create()
{
  // Create a renderer in RENDERER_LAYER that will display the color legend
  // above the default layer (above images and markups).
  vtkRenderer* renderer = this->GetRenderer();
  if (!renderer)
    {
    vtkErrorMacro("vtkMRMLColorLegendDisplayableManager::Create() failed: renderer is invalid");
    return;
    }
  this->Internal->ColorLegendRenderer->InteractiveOff();
  vtkRenderWindow* renderWindow = renderer->GetRenderWindow();
  if (!renderer)
    {
    vtkErrorMacro("vtkMRMLColorLegendDisplayableManager::Create() failed: render window is invalid");
    return;
    }
  if (renderWindow->GetNumberOfLayers() < RENDERER_LAYER + 1)
    {
    renderWindow->SetNumberOfLayers(RENDERER_LAYER + 1);
    }
  this->Internal->ColorLegendRenderer->SetLayer(RENDERER_LAYER);
  renderWindow->AddRenderer(this->Internal->ColorLegendRenderer);

  // TODO: needed?
  // this->Internal->UpdateSliceNode();
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::AdditionalInitializeStep()
{
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::SetMRMLSceneInternal(vtkMRMLScene * newScene)
{
  vtkNew<vtkIntArray> events;
  events->InsertNextValue(vtkMRMLScene::NodeAddedEvent);
  events->InsertNextValue(vtkMRMLScene::NodeRemovedEvent);
  events->InsertNextValue(vtkMRMLScene::EndCloseEvent);
  events->InsertNextValue(vtkMRMLScene::EndBatchProcessEvent);
  this->SetAndObserveMRMLSceneEventsInternal(newScene, events.GetPointer());
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::OnMRMLDisplayableNodeModifiedEvent(vtkObject* vtkNotUsed(caller))
{
  // slice node has been updated, nothing to do
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::OnMRMLSceneNodeAdded(vtkMRMLNode* node)
{
  this->Superclass::OnMRMLSceneNodeAdded(node);

  if (!node || !this->GetMRMLScene())
    {
    vtkErrorMacro("OnMRMLSceneNodeAdded: Invalid MRML scene or input node");
    return;
    }

  if (node->IsA("vtkMRMLColorLegendDisplayNode"))
    {
    vtkNew<vtkIntArray> events;
    events->InsertNextValue(vtkCommand::ModifiedEvent);
    vtkObserveMRMLNodeEventsMacro(node, events);

    vtkNew<vtkSlicerScalarBarActor> scalarBarActor;
    scalarBarActor->UnconstrainedFontSizeOn();

    // By default, color swatch is too wide (especially when showing long color names),
    // therefore, set it to a bit narrower
    scalarBarActor->SetBarRatio(0.2);

    std::string id(node->GetID());
    this->Internal->ColorLegendActorsMap[id] = scalarBarActor;

    this->ProcessMRMLNodesEvents(node, vtkCommand::ModifiedEvent, nullptr);
    }
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::OnMRMLSceneNodeRemoved(vtkMRMLNode* node)
{
  this->Superclass::OnMRMLSceneNodeRemoved(node);

  if (!node || !this->GetMRMLScene())
    {
    vtkErrorMacro("OnMRMLSceneNodeRemoved: Invalid MRML scene or input node");
    return;
    }

  if (node->IsA("vtkMRMLColorLegendDisplayNode"))
    {
    vtkUnObserveMRMLNodeMacro(node);

    vtkMRMLColorLegendDisplayNode* dispNode = vtkMRMLColorLegendDisplayNode::SafeDownCast(node);

    auto it = this->Internal->ColorLegendActorsMap.find(node->GetID());
    if (it != this->Internal->ColorLegendActorsMap.end())
      {
      vtkSmartPointer<vtkSlicerScalarBarActor> actor = it->second;
      this->Internal->ColorLegendActorsMap.erase(it);
      this->Internal->ColorLegendRenderer->RemoveActor(actor);
      }
    }
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::UpdateFromMRML()
{
  // this gets called from RequestRender, so make sure to jump out quickly if possible
  if (this->GetMRMLScene() == nullptr)
    {
    return;
    }

  // This is called when the view node is set. Update all actors.
  for (auto& colorBarNodeIdToActorIt : this->Internal->ColorLegendActorsMap)
    {
    vtkMRMLColorLegendDisplayNode* displayNode = vtkMRMLColorLegendDisplayNode::SafeDownCast(
      this->GetMRMLScene()->GetNodeByID(colorBarNodeIdToActorIt.first));
    if (!displayNode)
      {
      // orphan pipeline, it should have been deleted by the node removed event notification
      vtkWarningMacro("vtkMRMLColorLegendDisplayableManager::UpdateFromMRML: invalid node ID " << colorBarNodeIdToActorIt.first);
      continue;
      }
    this->Internal->UpdateActor(displayNode);
    }
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::ProcessMRMLNodesEvents(vtkObject *caller, unsigned long event, void *callData)
{
  this->Superclass::ProcessMRMLNodesEvents(caller, event, callData);

  if (event != vtkCommand::ModifiedEvent)
    {
    return;
    }
  vtkMRMLColorLegendDisplayNode* dispNode = vtkMRMLColorLegendDisplayNode::SafeDownCast(caller);
  vtkMRMLSliceCompositeNode* sliceCompositeNode = vtkMRMLSliceCompositeNode::SafeDownCast(caller);
  if (dispNode)
    {
    if (this->Internal->UpdateActor(dispNode))
      {
      this->RequestRender();
      }
    }
  else if (sliceCompositeNode)
    {
    this->SetUpdateFromMRMLRequested(true);
    this->RequestRender();
    }
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::UpdateFromMRMLScene()
{
  this->Internal->UpdateSliceNode();
}

//---------------------------------------------------------------------------
void vtkMRMLColorLegendDisplayableManager::UnobserveMRMLScene()
{
  this->Internal->SetSliceCompositeNode(nullptr);
}
