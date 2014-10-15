/********************************************************************* *\
 * INTEL CORPORATION PROPRIETARY INFORMATION                            
 * This software is supplied under the terms of a license agreement or  
 * nondisclosure agreement with Intel Corporation and may not be copied 
 * or disclosed except in accordance with the terms of that agreement.  
 * Copyright (C) 2014 Intel Corporation. All Rights Reserved.           
 ********************************************************************* */

#include "TransferFunctionEditor.h"

TransferFunctionEditor::TransferFunctionEditor(OSPTransferFunction transferFunction)
{
    // assign transfer function
    if(!transferFunction)
        throw std::runtime_error("must be constructed with an existing transfer function");

    transferFunction_ = transferFunction;

    // load color maps
    loadColorMaps();

    // setup UI elments
    QVBoxLayout * layout = new QVBoxLayout();
    setLayout(layout);

    // save and load buttons
    QWidget * saveLoadWidget = new QWidget();
    QHBoxLayout * hboxLayout = new QHBoxLayout();
    saveLoadWidget->setLayout(hboxLayout);

    QPushButton * saveButton = new QPushButton("Save");
    connect(saveButton, SIGNAL(clicked()), this, SLOT(save()));
    hboxLayout->addWidget(saveButton);

    QPushButton * loadButton = new QPushButton("Load");
    connect(loadButton, SIGNAL(clicked()), this, SLOT(load()));
    hboxLayout->addWidget(loadButton);

    layout->addWidget(saveLoadWidget);

    // form layout
    QWidget * formWidget = new QWidget();
    QFormLayout * formLayout = new QFormLayout();
    formWidget->setLayout(formLayout);
    layout->addWidget(formWidget);

    // color map choice
    for(unsigned int i=0; i<colorMaps_.size(); i++)
    {
        colorMapComboBox_.addItem(colorMaps_[i].getName().c_str());
    }

    formLayout->addRow("Color map", &colorMapComboBox_);

    // data value range, used as the domain for both color and opacity components of the transfer function
    dataValueMinSpinBox_.setRange(-999999., 999999.);
    dataValueMaxSpinBox_.setRange(-999999., 999999.);
    dataValueMinSpinBox_.setValue(0.);
    dataValueMaxSpinBox_.setValue(1.);
    dataValueMinSpinBox_.setDecimals(6);
    dataValueMaxSpinBox_.setDecimals(6);
    formLayout->addRow("Data value min", &dataValueMinSpinBox_);
    formLayout->addRow("Data value max", &dataValueMaxSpinBox_);

    // opacity transfer function widget
    layout->addWidget(&transferFunctionAlphaWidget_);

    //! The Qt 4.8 documentation says: "by default, for every connection you
    //! make, a signal is emitted".  But this isn't happening here (Qt 4.8.5,
    //! Mac OS 10.9.4) so we manually invoke these functions so the transfer
    //! function is fully populated before the first render call.
    //!
    //! Unfortunately, each invocation causes all transfer function fields to
    //! be rewritten on the ISPC side (due to repeated recommits of the OSPRay
    //! transfer function object).
    //!
    setColorMapIndex(0);
    transferFunctionAlphasChanged();
    setDataValueMin(0.0);
    setDataValueMax(1.0);

    connect(&colorMapComboBox_, SIGNAL(currentIndexChanged(int)), this, SLOT(setColorMapIndex(int)));
    connect(&dataValueMinSpinBox_, SIGNAL(valueChanged(double)), this, SLOT(setDataValueMin(double)));
    connect(&dataValueMaxSpinBox_, SIGNAL(valueChanged(double)), this, SLOT(setDataValueMax(double)));
    connect(&transferFunctionAlphaWidget_, SIGNAL(transferFunctionChanged()), this, SLOT(transferFunctionAlphasChanged()));

}

void TransferFunctionEditor::transferFunctionAlphasChanged()
{

    // default to 256 discretizations of the opacities over the domain
    std::vector<float> transferFunctionAlphas = transferFunctionAlphaWidget_.getInterpolatedValuesOverInterval(256);

    OSPData transferFunctionAlphasData = ospNewData(transferFunctionAlphas.size(), OSP_FLOAT, transferFunctionAlphas.data());
    ospSetData(transferFunction_, "alphas", transferFunctionAlphasData);

    // commit and emit signal
    ospCommit(transferFunction_);
    emit transferFunctionChanged();

}

void TransferFunctionEditor::load(std::string filename)
{
    //! Get filename if not specified.
    if(filename.empty())
        filename = QFileDialog::getOpenFileName(this, tr("Load transfer function"), ".", "Transfer function files (*.tfn)").toStdString();

    if(filename.empty())
        return;

    //! Get serialized transfer function state from file.
    QFile file(filename.c_str());
    bool success = file.open(QIODevice::ReadOnly);

    if(!success)
    {
        std::cerr << "unable to open " << filename << std::endl;
        return;
    }

    QDataStream in(&file);

    int colorMapIndex;
    in >> colorMapIndex;

    double dataValueMin, dataValueMax;
    in >> dataValueMin >> dataValueMax;

    QVector<QPointF> points;
    in >> points;

    //! Update transfer function state. Update values of the UI elements directly to signal appropriate slots.
    colorMapComboBox_.setCurrentIndex(colorMapIndex);
    dataValueMinSpinBox_.setValue(dataValueMin);
    dataValueMaxSpinBox_.setValue(dataValueMax);
    transferFunctionAlphaWidget_.setPoints(points);
}

void TransferFunctionEditor::save()
{
    //! Get filename.
    QString filename = QFileDialog::getSaveFileName(this, "Save transfer function", ".", "Transfer function files (*.tfn)");

    if(filename.isNull())
        return;

    //! Make sure the filename has the proper extension.
    if(filename.endsWith(".tfn") != true)
        filename += ".tfn";

    //! Serialize transfer function state to file.
    QFile file(filename);
    bool success = file.open(QIODevice::WriteOnly);

    if(!success)
    {
        std::cerr << "unable to open " << filename.toStdString() << std::endl;
        return;
    }

    QDataStream out(&file);

    out << colorMapComboBox_.currentIndex();
    out << dataValueMinSpinBox_.value();
    out << dataValueMaxSpinBox_.value();
    out << transferFunctionAlphaWidget_.getPoints();
}

void TransferFunctionEditor::setColorMapIndex(int index)
{
    // set transfer function color properties for this color map
    std::vector<osp::vec3f> colors = colorMaps_[index].getColors();

    OSPData transferFunctionColorsData = ospNewData(colors.size(), OSP_FLOAT3, colors.data());
    ospSetData(transferFunction_, "colors", transferFunctionColorsData);

    // set transfer function widget background image
    transferFunctionAlphaWidget_.setBackgroundImage(colorMaps_[index].getImage());

    // commit and emit signal
    ospCommit(transferFunction_);
    emit transferFunctionChanged();

}

void TransferFunctionEditor::setDataValueMin(double value)
{

    // set as the minimum value in the domain for both color and opacity components of the transfer function
    ospSetf(transferFunction_, "colorValueMin", float(value));
    ospSetf(transferFunction_, "alphaValueMin", float(value));

    // commit and emit signal
    ospCommit(transferFunction_);
    emit transferFunctionChanged();

}

void TransferFunctionEditor::setDataValueMax(double value)
{

    // set as the maximum value in the domain for both color and opacity components of the transfer function
    ospSetf(transferFunction_, "colorValueMax", float(value));
    ospSetf(transferFunction_, "alphaValueMax", float(value));

    // commit and emit signal
    ospCommit(transferFunction_);
    emit transferFunctionChanged();

}

void TransferFunctionEditor::loadColorMaps()
{
    // color maps based on ParaView default color maps

    std::vector<osp::vec3f> colors;

    // jet
    colors.clear();
    colors.push_back(osp::vec3f(0         , 0           , 0.562493   ));
    colors.push_back(osp::vec3f(0         , 0           , 1          ));
    colors.push_back(osp::vec3f(0         , 1           , 1          ));
    colors.push_back(osp::vec3f(0.500008  , 1           , 0.500008   ));
    colors.push_back(osp::vec3f(1         , 1           , 0          ));
    colors.push_back(osp::vec3f(1         , 0           , 0          ));
    colors.push_back(osp::vec3f(0.500008  , 0           , 0          ));
    colorMaps_.push_back(ColorMap("Jet", colors));

    // ice / fire
    colors.clear();
    colors.push_back(osp::vec3f(0         , 0           , 0           ));
    colors.push_back(osp::vec3f(0         , 0.120394    , 0.302678    ));
    colors.push_back(osp::vec3f(0         , 0.216587    , 0.524575    ));
    colors.push_back(osp::vec3f(0.0552529 , 0.345022    , 0.659495    ));
    colors.push_back(osp::vec3f(0.128054  , 0.492592    , 0.720287    ));
    colors.push_back(osp::vec3f(0.188952  , 0.641306    , 0.792096    ));
    colors.push_back(osp::vec3f(0.327672  , 0.784939    , 0.873426    ));
    colors.push_back(osp::vec3f(0.60824   , 0.892164    , 0.935546    ));
    colors.push_back(osp::vec3f(0.881376  , 0.912184    , 0.818097    ));
    colors.push_back(osp::vec3f(0.9514    , 0.835615    , 0.449271    ));
    colors.push_back(osp::vec3f(0.904479  , 0.690486    , 0           ));
    colors.push_back(osp::vec3f(0.854063  , 0.510857    , 0           ));
    colors.push_back(osp::vec3f(0.777096  , 0.330175    , 0.000885023 ));
    colors.push_back(osp::vec3f(0.672862  , 0.139086    , 0.00270085  ));
    colors.push_back(osp::vec3f(0.508812  , 0           , 0           ));
    colors.push_back(osp::vec3f(0.299413  , 0.000366217 , 0.000549325 ));
    colors.push_back(osp::vec3f(0.0157473 , 0.00332647  , 0           ));
    colorMaps_.push_back(ColorMap("Ice / Fire", colors));

    // cool to warm
    colors.clear();
    colors.push_back(osp::vec3f(0.231373  , 0.298039    , 0.752941    ));
    colors.push_back(osp::vec3f(0.865003  , 0.865003    , 0.865003    ));
    colors.push_back(osp::vec3f(0.705882  , 0.0156863   , 0.14902     ));
    colorMaps_.push_back(ColorMap("Cool to Warm", colors));

    // blue to red rainbow
    colors.clear();
    colors.push_back(osp::vec3f(0         , 0           , 1           ));
    colors.push_back(osp::vec3f(1         , 0           , 0           ));
    colorMaps_.push_back(ColorMap("Blue to Red Rainbow", colors));

    // grayscale
    colors.clear();
    colors.push_back(osp::vec3f(0.));
    colors.push_back(osp::vec3f(1.));
    colorMaps_.push_back(ColorMap("Grayscale", colors));
}
